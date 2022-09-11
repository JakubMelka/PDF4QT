//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolfetchimages.h"
#include "pdfpagecontentprocessor.h"
#include "pdfconstants.h"
#include "pdfexecutionpolicy.h"

#include <QCryptographicHash>

namespace pdftool
{

static PDFToolFetchImages s_fetchImagesApplication;

class PDFImageContentExtractorProcessor : public pdf::PDFPageContentProcessor
{
    using BaseClass = PDFPageContentProcessor;

public:
    explicit PDFImageContentExtractorProcessor(const pdf::PDFPage* page,
                                               const pdf::PDFDocument* document,
                                               const pdf::PDFFontCache* fontCache,
                                               const pdf::PDFCMS* cms,
                                               const pdf::PDFOptionalContentActivity* optionalContentActivity,
                                               QTransform pagePointToDevicePointMatrix,
                                               const pdf::PDFMeshQualitySettings& meshQualitySettings,
                                               pdf::PDFInteger pageIndex,
                                               PDFToolFetchImages* tool) :
        BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
        m_pageIndex(pageIndex),
        m_order(0),
        m_tool(tool)
    {

    }

protected:
    virtual bool isContentSuppressedByOC(pdf::PDFObjectReference ocgOrOcmd) override;
    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performImagePainting(const QImage& image) override;

private:
    pdf::PDFInteger m_pageIndex;
    pdf::PDFInteger m_order;
    PDFToolFetchImages* m_tool;
};

bool PDFImageContentExtractorProcessor::isContentSuppressedByOC(pdf::PDFObjectReference ocgOrOcmd)
{
    Q_UNUSED(ocgOrOcmd);
    return false;
}

bool PDFImageContentExtractorProcessor::isContentKindSuppressed(ContentKind kind) const
{
    switch (kind)
    {
        case ContentKind::Shapes:
        case ContentKind::Text:
        case ContentKind::Shading:
            return true;

        case ContentKind::Tiling:
        case ContentKind::Images:
            return false; // Tiling can have images

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    return false;
}


void PDFImageContentExtractorProcessor::performImagePainting(const QImage& image)
{
    m_tool->onImageExtracted(m_pageIndex, m_order++, image);
}

QString PDFToolFetchImages::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "fetch-images";

        case Name:
            return PDFToolTranslationContext::tr("Fetch images");

        case Description:
            return PDFToolTranslationContext::tr("Fetch image content from document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolFetchImages::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    if (!document.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Document doesn't allow to copy content."), options.outputCodec);
        return ErrorPermissions;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

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
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(options.cmsSettings);
    pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFModifiedDocument md(&document, &optionalContentActivity);
    fontCache.setDocument(md);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    auto processPageContents = [&, this](pdf::PDFInteger pageIndex)
    {
        const pdf::PDFCatalog* catalog = document.getCatalog();
        if (!catalog->getPage(pageIndex))
        {
            // Invalid page index
            return;
        }

        const pdf::PDFPage* page = catalog->getPage(pageIndex);
        Q_ASSERT(page);

        PDFImageContentExtractorProcessor processor(page, &document, &fontCache, cms.data(), &optionalContentActivity,
                                                    QTransform(), meshQualitySettings, pageIndex, this);
        processor.processContents();
    };

    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Page, pageIndices.begin(), pageIndices.end(), processPageContents);
    fontCache.setCacheShrinkEnabled(nullptr, true);

    auto comparator = [](const Image& left, const Image& right) -> bool
    {
        return std::make_pair(left.pageIndex, left.order) < std::make_pair(right.pageIndex, right.order);
    };
    std::sort(m_images.begin(), m_images.end(), comparator);

    // Write information about images
    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("images", PDFToolTranslationContext::tr("Images fetched from document %1").arg(options.document));
    formatter.endl();

    formatter.beginTable("overview", PDFToolTranslationContext::tr("Overview"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("item-no", PDFToolTranslationContext::tr("Image No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("page-no", PDFToolTranslationContext::tr("Page No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("width", PDFToolTranslationContext::tr("Width [pixels]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("height", PDFToolTranslationContext::tr("Height [pixels]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("size", PDFToolTranslationContext::tr("Size [bytes]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("stored-to", PDFToolTranslationContext::tr("Stored to"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    QLocale locale;

    for (size_t i = 0; i < m_images.size(); ++i)
    {
        Image& image = m_images[i];
        image.fileName = options.imageExportSettings.getOutputFileName(pdf::PDFInteger(i), options.imageWriterSettings.getCurrentFormat());

        formatter.beginTableRow("image", int(i));

        formatter.writeTableColumn("item-no", locale.toString(i + 1), Qt::AlignRight);
        formatter.writeTableColumn("page-no", locale.toString(image.pageIndex + 1), Qt::AlignRight);
        formatter.writeTableColumn("width", locale.toString(image.image.width()), Qt::AlignRight);
        formatter.writeTableColumn("height", locale.toString(image.image.height()), Qt::AlignRight);
        formatter.writeTableColumn("size", locale.toString(image.image.sizeInBytes()), Qt::AlignRight);
        formatter.writeTableColumn("stored-to", image.fileName);

        formatter.endTableRow();
    }

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    // Store images to the disk file
    auto saveImage = [this, &options](size_t index)
    {
        Image& image = m_images[index];

        QImageWriter imageWriter(image.fileName, options.imageWriterSettings.getCurrentFormat());
        imageWriter.setSubType(options.imageWriterSettings.getCurrentSubtype());
        imageWriter.setCompression(options.imageWriterSettings.getCompression());
        imageWriter.setQuality(options.imageWriterSettings.getQuality());
        imageWriter.setOptimizedWrite(options.imageWriterSettings.hasOptimizedWrite());
        imageWriter.setProgressiveScanWrite(options.imageWriterSettings.hasProgressiveScanWrite());

        if (!imageWriter.write(image.image))
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot write page image to file '%1', because: %2.").arg(image.fileName).arg(imageWriter.errorString()), options.outputCodec);
        }
    };

    auto imageRange = pdf::PDFIntegerRange<size_t>(0, m_images.size());
    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Page, imageRange.begin(), imageRange.end(), saveImage);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolFetchImages::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ImageWriterSettings | ImageExportSettingsFiles | ColorManagementSystem;
}

void PDFToolFetchImages::onImageExtracted(pdf::PDFInteger pageIndex, pdf::PDFInteger order, const QImage& image)
{
    QCryptographicHash hasher(QCryptographicHash::Sha512);
    QByteArrayView imageData(image.bits(), image.sizeInBytes());
    hasher.addData(imageData);
    QByteArray hash = hasher.result();

    QMutexLocker lock(&m_mutex);
    auto it = std::find_if(m_images.begin(), m_images.end(), [&hash](const Image& image) { return image.hash == hash; });
    if (it == m_images.cend())
    {
        Image imageStructure;
        imageStructure.hash = hash;
        imageStructure.pageIndex = pageIndex;
        imageStructure.order = order;
        imageStructure.image = image;
        m_images.emplace_back(qMove(imageStructure));
    }
    else
    {
        Image& imageStructure = *it;
        if (imageStructure.pageIndex > pageIndex)
        {
            imageStructure.pageIndex = pageIndex;
            imageStructure.order = order;
        }
    }
}

}   // namespace pdftool
