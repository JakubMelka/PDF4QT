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

#include "pdftoolbitonal.h"

#include "pdfconstants.h"
#include "pdfdocumentwriter.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"

#include <algorithm>
#include <map>
#include <vector>

namespace pdftool
{

static PDFToolBitonal s_bitonalApplication;

QString PDFToolBitonal::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "bitonal";

        case Name:
            return PDFToolTranslationContext::tr("Bitonal Document");

        case Description:
            return PDFToolTranslationContext::tr("Convert a document into a bitonal (monochromatic) one.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolBitonal::execute(const PDFToolOptions& options)
{
    if (!options.bitonalInvalidArgument.isEmpty())
    {
        PDFConsole::writeError(options.bitonalInvalidArgument, options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.bitonalDocument.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No output document specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.bitonalThreshold < 0 || options.bitonalThreshold > 255)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Threshold must be in range 0 to 255."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    const bool isPageSource = options.bitonalSource == pdf::PDFBitonalDocumentCreator::ConversionSource::Pages;

    if (!isPageSource && options.isPageRangeSet())
    {
        // Images are not bound to a single page - the same image can be used on many
        // pages and images inside form XObjects are not reachable from the page
        // resources at all, so a page range cannot be honored reliably here.
        PDFConsole::writeError(PDFToolTranslationContext::tr("Page range can be used only with '--bitonal-source pages'."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (!isPageSource && options.bitonalDetectBlankPages)
    {
        // A blank page is a property of the page, a single image of a page carries no
        // information about it
        PDFConsole::writeError(PDFToolTranslationContext::tr("The option '--bitonal-detect-blank' can be used only with '--bitonal-source pages'."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.bitonalDpiResolution != 0 &&
        (options.bitonalDpiResolution < pdf::PDFBitonalDocumentCreator::MINIMUM_DPI_RESOLUTION ||
         options.bitonalDpiResolution > pdf::PDFBitonalDocumentCreator::MAXIMUM_DPI_RESOLUTION))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Resolution must be zero, or in range %1 to %2 DPI.").arg(pdf::PDFBitonalDocumentCreator::MINIMUM_DPI_RESOLUTION)
                                                                                                                 .arg(pdf::PDFBitonalDocumentCreator::MAXIMUM_DPI_RESOLUTION), options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    // Pages are rasterized, so the whole rendering machinery is needed. It is created
    // even for the image source - it costs almost nothing and it keeps the code simple.
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(options.cmsSettings);
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFModifiedDocument modifiedDocument(&document, &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    // Renderer features are not configurable - the conversion needs its own fixed set
    // of them. The software rasterizer works everywhere, which is what a command line
    // tool needs.
    pdf::PDFRasterizerPool rasterizerPool(&document, &fontCache, &cmsManager, &optionalContentActivity,
                                          pdf::PDFBitonalDocumentCreator::getPageRasterizationFeatures(),
                                          meshQualitySettings,
                                          pdf::PDFRasterizerPool::getDefaultRasterizerCount(),
                                          pdf::RendererEngine::QPainter,
                                          nullptr);

    pdf::PDFBitonalDocumentCreator creator(&document, &rasterizerPool, nullptr);

    pdf::PDFBitonalDocumentCreator::Settings settings;
    settings.conversionSource = options.bitonalSource;
    settings.conversionMethod = options.bitonalMethod;
    settings.manualThreshold = options.bitonalThreshold;
    settings.dpiResolution = options.bitonalDpiResolution != 0 ? options.bitonalDpiResolution : creator.getEstimatedDpiResolution();
    settings.compression = options.bitonalCompression;

    if (isPageSource)
    {
        QString parseError;
        const std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

        if (!parseError.isEmpty())
        {
            PDFConsole::writeError(parseError, options.outputCodec);
            return ErrorInvalidArguments;
        }

        // Pages, which are a scan of a blank sheet of paper. A page is examined only
        // when the user has asked for it - the detection rasterizes the whole document
        // once more, and a page, which is wrongly taken for a blank one, loses its
        // content irreversibly.
        std::vector<char> blankPages(pageIndices.size(), 0);

        if (options.bitonalDetectBlankPages && !pageIndices.empty())
        {
            // The detection has a resolution of its own, see PDFBitonalDocumentCreator
            constexpr int dpiResolution = pdf::PDFBitonalDocumentCreator::BLANK_PAGE_DPI_RESOLUTION;

            std::map<pdf::PDFInteger, size_t> itemIndices;
            for (size_t index = 0; index < pageIndices.size(); ++index)
            {
                itemIndices[pageIndices[index]] = index;
            }

            auto pageSizeGetter = [dpiResolution](const pdf::PDFPage* page) -> QSize
            {
                return pdf::PDFBitonalDocumentCreator::getPageImageSize(page, dpiResolution);
            };

            auto pageImageProcessor = [dpiResolution, &itemIndices, &blankPages](pdf::PDFInteger pageIndex, QImage image)
            {
                auto it = itemIndices.find(pageIndex);

                if (it != itemIndices.cend() && pdf::PDFBitonalDocumentCreator::detectBlankPage(image, dpiResolution, nullptr).isBlank)
                {
                    // Pages are examined in parallel, but each of them has an element
                    // of its own, so the threads never write into the same one
                    blankPages[it->second] = char(1);
                }
            };

            try
            {
                creator.renderPages(pageIndices, pageSizeGetter, pageImageProcessor, nullptr);
            }
            catch (...)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Blank page detection failed. The output document has not been written."), options.outputCodec);
                return ErrorUnknown;
            }

            const auto blankPageCount = std::count(blankPages.cbegin(), blankPages.cend(), char(1));
            PDFConsole::writeError(PDFToolTranslationContext::tr("Note: %1 of %2 pages have been detected as blank and they are replaced by a white fill.")
                                       .arg(blankPageCount)
                                       .arg(pageIndices.size()), options.outputCodec);
        }

        for (size_t index = 0; index < pageIndices.size(); ++index)
        {
            pdf::PDFBitonalDocumentCreator::ItemInfo item;
            item.pageIndex = pageIndices[index];
            item.mode = blankPages[index] != 0 ? pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite : options.bitonalItemMode;
            settings.items.push_back(item);
        }
    }
    else
    {
        for (const pdf::PDFObjectReference reference : creator.getConvertibleImages())
        {
            pdf::PDFBitonalDocumentCreator::ItemInfo item;
            item.imageReference = reference;
            item.mode = options.bitonalItemMode;
            settings.items.push_back(item);
        }
    }

    if (settings.items.empty())
    {
        PDFConsole::writeError(isPageSource ? PDFToolTranslationContext::tr("No page has been selected for the conversion.")
                                            : PDFToolTranslationContext::tr("Document does not contain any convertible image."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    const bool isConverted = creator.createBitonalDocument(settings);

    fontCache.setCacheShrinkEnabled(nullptr, true);

    if (!isConverted)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to convert the document into the bitonal format."), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    if (creator.getFailedItemCount() > 0)
    {
        // The document has been created, but a part of it could not be converted and it
        // is left in the original, colored form. The user has to know about it.
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: %1 of %2 items could not be converted and they are left unchanged.")
                                   .arg(creator.getFailedItemCount())
                                   .arg(creator.getFailedItemCount() + creator.getConvertedItemCount()), options.outputCodec);
    }

    pdf::PDFDocument bitonalDocument = creator.takeBitonalDocument();

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(options.bitonalDocument, &bitonalDocument, true);
    if (!result)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write bitonal document. %1").arg(result.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolBitonal::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ColorManagementSystem | Bitonal;
}

}   // namespace pdftool
