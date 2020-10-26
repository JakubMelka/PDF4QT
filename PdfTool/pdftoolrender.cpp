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
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolrender.h"

namespace pdftool
{

static PDFToolRender s_toolRenderApplication;
static PDFToolBenchmark s_toolBenchmarkApplication;

QString PDFToolRender::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "render";

        case Name:
            return PDFToolTranslationContext::tr("Render document");

        case Description:
            return PDFToolTranslationContext::tr("Render selected pages of document into image files.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolRender::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ImageWriterSettings | ImageExportSettingsFiles | ImageExportSettingsResolution | ColorManagementSystem | RenderFlags;
}

QString PDFToolBenchmark::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "benchmark";

        case Name:
            return PDFToolTranslationContext::tr("Benchmark rendering");

        case Description:
            return PDFToolTranslationContext::tr("Benchmark page rendering (measure time, detect errors).");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

PDFToolAbstractApplication::Options PDFToolBenchmark::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ImageExportSettingsResolution | ColorManagementSystem | RenderFlags;
}

int PDFToolRenderBase::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData))
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

    QString errorMessage;
    Options optionFlags = getOptionsFlags();
    if (!options.imageExportSettings.validate(&errorMessage, false, optionFlags.testFlag(ImageExportSettingsFiles), optionFlags.testFlag(ImageExportSettingsResolution)))
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // We are ready to render the document
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    m_cms = m_proxy->getCMSManager()->getCurrentCMS();
    pdf::PDFRasterizerPool rasterizerPool(&document, m_proxy->getFontCache(), m_proxy->getCMSManager(),
                                          &optionalContentActivity, m_proxy->getFeatures(), m_proxy->getMeshQualitySettings(),
                                          pdf::PDFRasterizerPool::getDefaultRasterizerCount(), m_proxy->isUsingOpenGL(), m_proxy->getSurfaceFormat(), this);
    connect(&rasterizerPool, &pdf::PDFRasterizerPool::renderError, this, &PDFRenderToImagesDialog::onRenderError);

    auto imageSizeGetter = [&options](const pdf::PDFPage* page) -> QSize
    {
        Q_ASSERT(page);

        switch (options.imageExportSettings.getResolutionMode())
        {
            case pdf::PDFPageImageExportSettings::ResolutionMode::DPI:
            {
                QSizeF size = page->getRotatedMediaBox().size() * pdf::PDF_POINT_TO_INCH * options.imageExportSettings.getDpiResolution();
                return size.toSize();
            }

            case pdf::PDFPageImageExportSettings::ResolutionMode::Pixels:
            {
                int pixelResolution = options.imageExportSettings.getPixelResolution();
                QSizeF size = page->getRotatedMediaBox().size().scaled(pixelResolution, pixelResolution, Qt::KeepAspectRatio);
                return size.toSize();
            }

            default:
            {
                Q_ASSERT(false);
                break;
            }
        }

        return QSize();
    };

    auto processImage = [this](const pdf::PDFInteger pageIndex, QImage&& image)
    {
        QString fileName = m_imageExportSettings.getOutputFileName(pageIndex, m_imageWriterSettings.getCurrentFormat());

        QImageWriter imageWriter(fileName, m_imageWriterSettings.getCurrentFormat());
        imageWriter.setSubType(m_imageWriterSettings.getCurrentSubtype());
        imageWriter.setCompression(m_imageWriterSettings.getCompression());
        imageWriter.setQuality(m_imageWriterSettings.getQuality());
        imageWriter.setGamma(m_imageWriterSettings.getGamma());
        imageWriter.setOptimizedWrite(m_imageWriterSettings.hasOptimizedWrite());
        imageWriter.setProgressiveScanWrite(m_imageWriterSettings.hasProgressiveScanWrite());

        if (!imageWriter.write(image))
        {
            emit m_rasterizerPool->renderError(pdf::PDFRenderError(pdf::RenderErrorType::Error, tr("Can't write page image to file '%1', because: %2.").arg(fileName).arg(imageWriter.errorString())));
        }
    };

    rasterizerPool.render(pageIndices, imageSizeGetter, processImage, m_progress);

    return ExitSuccess;
}

}   // namespace pdftool
