//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

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

    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
    formatter.beginDocument("ink-coverage", PDFToolTranslationContext::tr("Ink Coverage"));
    formatter.endl();

    formatter.beginTable("ink-coverage-by-page", PDFToolTranslationContext::tr("Ink Coverage by Page"));

    bool isHeaderProcessed = false;
    std::vector<pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo> headerCoverage;
    for (const pdf::PDFInteger pageIndex : pageIndices)
    {
        const std::vector<pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo>* coverage = calculator.getInkCoverage(pageIndex);

        if (!coverage)
        {
            // Jakub Melka: Something bad happened?
            continue;
        }

        if (!isHeaderProcessed)
        {
            formatter.beginTableHeaderRow("header");
            formatter.writeTableHeaderColumn("page-no", PDFToolTranslationContext::tr("Page No."), Qt::AlignLeft);
            for (const pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo& info : *coverage)
            {
                formatter.writeTableHeaderColumn(QString("%1-ratio").arg(QString::fromLatin1(info.name)), PDFToolTranslationContext::tr("%1 Ratio [%]").arg(info.textName), Qt::AlignLeft);
                formatter.writeTableHeaderColumn(QString("%1-area").arg(QString::fromLatin1(info.name)), PDFToolTranslationContext::tr("%1 Covered [mm^2]").arg(info.textName), Qt::AlignLeft);
            }
            headerCoverage = *coverage;
            formatter.endTableHeaderRow();

            isHeaderProcessed = true;
        }

        for (const auto& info : *headerCoverage)
        {
            pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo* channelInfo = pdf::PDFInkCoverageCalculator::findCoverageInfoByName(*coverage, info.name);
            pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo* sumChannelInfo = pdf::PDFInkCoverageCalculator::findCoverageInfoByName(headerCoverage, info.name);

            if (sumChannelInfo && channelInfo)
            {
                sumChannelInfo->coveredArea += channelInfo->coveredArea;
            }

        }
    }

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
