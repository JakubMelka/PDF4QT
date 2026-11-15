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

#include "pdfbitonaldocumentcreator.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentimagelist.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfoptionalcontent.h"
#include "pdfnametreeloader.h"
#include "pdfnumbertreeloader.h"
#include "pdfoptimizer.h"
#include "pdfpage.h"
#include "pdfprogress.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"

#include <QPainter>
#include <QScopeGuard>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "pdfdbgheap.h"

namespace pdf
{

/// Entries of the original image dictionary, which are still valid for the converted
/// bitonal image. Entries describing the image samples (size, color space, filters)
/// and the transparency (soft masks) are always replaced by the converted ones.
static constexpr const char* PRESERVED_IMAGE_DICTIONARY_KEYS[] =
{
    "Intent",
    "Interpolate",
    "OC",
    "Metadata",
    "StructParent",
    "AF",
    "Measure",
    "PtData"
};

/// Composites the rasterized page onto the white background. Areas of the page,
/// which are not painted at all, are transparent in the rasterized image, but
/// they represent a blank paper, so they must become white.
static QImage compositePageImageOntoWhite(const QImage& image)
{
    if (image.isNull())
    {
        return image;
    }

    QImage result(image.size(), QImage::Format_RGB32);

    if (result.isNull())
    {
        // The image cannot be allocated
        return QImage();
    }

    result.fill(Qt::white);

    QPainter painter(&result);
    painter.drawImage(0, 0, image);
    painter.end();

    return result;
}

PDFBitonalDocumentCreator::PDFBitonalDocumentCreator(const PDFDocument* document,
                                                     PDFRasterizerPool* rasterizerPool,
                                                     PDFProgressSink* progress) :
    m_document(document),
    m_rasterizerPool(rasterizerPool),
    m_progress(progress)
{
    Q_ASSERT(m_document);
    m_iccProfileCache.setDocument(m_document);
}

PDFRenderer::Features PDFBitonalDocumentCreator::getPageRasterizationFeatures()
{
    return PDFRenderer::Features(PDFRenderer::Antialiasing |
                                 PDFRenderer::TextAntialiasing |
                                 PDFRenderer::SmoothImages |
                                 PDFRenderer::ClipToCropBox);
}

PDFBitonalRasterizationEnvironment::PDFBitonalRasterizationEnvironment(const PDFDocument* document,
                                                                       const PDFCMSSettings& cmsSettings,
                                                                       int rasterizerCount)
{
    Q_ASSERT(document);

    m_optionalContentActivity = std::make_unique<PDFOptionalContentActivity>(document, OCUsage::Export, nullptr);
    m_cmsManager = std::make_unique<PDFCMSManager>(nullptr);
    m_cmsManager->setDocument(document);
    m_cmsManager->setSettings(cmsSettings);
    m_fontCache = std::make_unique<PDFFontCache>(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    m_iccProfileCache = std::make_unique<PDFICCProfileCache>();

    // The caches only read the document; their interface takes a modifiable one
    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), m_optionalContentActivity.get());
    m_fontCache->setDocument(modifiedDocument);
    m_iccProfileCache->setDocument(document);

    // The pool renders in more threads at once: the cache must not drop a font, which another
    // thread is using
    m_fontCache->setCacheShrinkEnabled(nullptr, false);

    m_rasterizerPool = std::make_unique<PDFRasterizerPool>(document,
                                                           m_fontCache.get(),
                                                           m_iccProfileCache.get(),
                                                           m_cmsManager.get(),
                                                           m_optionalContentActivity.get(),
                                                           PDFBitonalDocumentCreator::getPageRasterizationFeatures(),
                                                           m_meshQualitySettings,
                                                           PDFRasterizerPool::getCorrectedRasterizerCount(rasterizerCount),
                                                           RendererEngine::QPainter,
                                                           nullptr);
}

PDFBitonalRasterizationEnvironment::~PDFBitonalRasterizationEnvironment()
{
    // The pool refers to everything else, so it goes first
    m_rasterizerPool.reset();
    m_fontCache->setCacheShrinkEnabled(nullptr, true);
}

bool PDFBitonalDocumentCreator::createBitonalDocument(const Settings& settings, const PDFOperationControl* operationControl)
{
    m_bitonalDocument = PDFDocument();
    m_convertedItemCount = 0;
    m_failedItemCount = 0;
    m_failedItems.clear();
    m_usedCompressions.clear();
    m_renderingIssues.clear();

    try
    {
        PDFDocumentBuilder builder(m_document);
        bool isConverted = false;
        std::set<PDFObjectReference> replacedImages;

        switch (settings.conversionSource)
        {
            case ConversionSource::Images:
                isConverted = createBitonalDocumentFromImages(builder, settings, operationControl, &replacedImages);
                break;

            case ConversionSource::Pages:
                isConverted = createBitonalDocumentFromPages(builder, settings, operationControl);
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        if (!isConverted || PDFOperationControl::isOperationCanceled(operationControl))
        {
            // Nothing has been converted, or the conversion has been canceled
            return false;
        }

        PDFDocument builtDocument = builder.build();

        // Images and content streams, which have been replaced, are not referenced
        // by the document anymore. They must be removed, otherwise the converted
        // document would be even larger than the original one. The objects keep their
        // numbers (the OCR layers are bound to them below).
        PDFOptimizer optimizer(PDFOptimizer::RemoveUnusedObjects, nullptr);
        optimizer.setDocument(&builtDocument);
        optimizer.optimize();

        // The version and the hash of the source stay (R4-9): the result is another revision
        // of the same document, its own OCR layers recognize it by the hash
        m_bitonalDocument = PDFDocument(optimizer.takeStorage(), m_document->getInfo()->version, m_document->getSourceDataHash());

        if (!replacedImages.empty())
        {
            // Only the samples of the images have changed, not their placement: the text
            // layers of PDF4QT over them stay valid and are bound to the new revision
            m_bitonalDocument = PDFOCRTextLayerWriter::rebindOptimizedDocument(m_document, std::move(m_bitonalDocument), replacedImages, nullptr);
        }
        return true;
    }
    catch (const PDFException&)
    {
        m_bitonalDocument = PDFDocument();
        return false;
    }
}

void PDFBitonalDocumentCreator::addFailedItem(const ItemInfo& item, FailureReason reason)
{
    ++m_failedItemCount;
    m_failedItems.push_back({ item, reason });
}

void PDFBitonalDocumentCreator::addUsedCompression(const PDFObject& imageObject)
{
    if (std::optional<Compression> compression = getImageObjectCompression(imageObject))
    {
        ++m_usedCompressions[*compression];
    }
}

QString PDFBitonalDocumentCreator::getFailureReasonText(FailureReason reason)
{
    switch (reason)
    {
        case FailureReason::CannotBeRead:
            return PDFTranslationContext::tr("cannot be read");
        case FailureReason::RenderingErrors:
            return PDFTranslationContext::tr("rendering errors");
        case FailureReason::TooLarge:
            return PDFTranslationContext::tr("too large");
        case FailureReason::CannotBeEncoded:
            return PDFTranslationContext::tr("cannot be encoded");
        case FailureReason::InvalidPage:
            return PDFTranslationContext::tr("invalid page");
    }

    Q_ASSERT(false);
    return QString();
}

QString PDFBitonalDocumentCreator::getCompressionName(Compression compression)
{
    switch (compression)
    {
        case Compression::Flate:
            return QStringLiteral("Flate");
        case Compression::RunLength:
            return QStringLiteral("RunLength");
        case Compression::CCITTGroup4:
            return QStringLiteral("CCITT G4");
        case Compression::JBIG2:
            return QStringLiteral("JBIG2");
        case Compression::Auto:
            break;
    }

    return QString();
}

std::optional<PDFBitonalDocumentCreator::Compression> PDFBitonalDocumentCreator::getImageObjectCompression(const PDFObject& imageObject)
{
    if (!imageObject.isStream())
    {
        return std::nullopt;
    }

    // The encoder writes one filter (a name); an array is read by its last filter
    PDFObject filter = imageObject.getStream()->getDictionary()->get("Filter");
    if (filter.isArray() && filter.getArray()->getCount() > 0)
    {
        filter = filter.getArray()->getItem(filter.getArray()->getCount() - 1);
    }

    if (!filter.isName())
    {
        return std::nullopt;
    }

    const QByteArray name = filter.getString();
    if (name == "FlateDecode")
    {
        return Compression::Flate;
    }
    if (name == "RunLengthDecode")
    {
        return Compression::RunLength;
    }
    if (name == "CCITTFaxDecode")
    {
        return Compression::CCITTGroup4;
    }
    if (name == "JBIG2Decode")
    {
        return Compression::JBIG2;
    }
    return std::nullopt;
}

QImage PDFBitonalDocumentCreator::createItemImage(const QImage& image,
                                                  ConversionSource source,
                                                  ItemMode mode,
                                                  PDFImageConversion::ConversionMethod conversionMethod,
                                                  int threshold,
                                                  QImage* alphaMask,
                                                  int* usedThreshold,
                                                  const PDFOperationControl* operationControl)
{
    if (image.isNull())
    {
        return QImage();
    }

    switch (mode)
    {
        case ItemMode::Algorithm:
        case ItemMode::AlgorithmInverted:
        {
            // A rendered page is composited onto the white paper already: it has no
            // transparency of its own
            QImage bitonalImage = convertImageToBitonal(image, conversionMethod, threshold, source == ConversionSource::Images ? alphaMask : nullptr, operationControl, usedThreshold);

            if (mode == ItemMode::AlgorithmInverted)
            {
                // Only the color samples are swapped - the alpha mask carries the
                // transparency of the image, which the inversion must not touch.
                bitonalImage = invertBitonalImage(std::move(bitonalImage));
            }
            return bitonalImage;
        }

        case ItemMode::FillBlack:
        case ItemMode::FillWhite:
        {
            if (source == ConversionSource::Pages)
            {
                // The page is covered by a solid area, which needs no rasterization
                return createFillImage(QSize(1, 1), mode == ItemMode::FillBlack);
            }

            // Only the color samples are replaced by the solid fill - the transparency
            // of the original image is preserved. Filling a masked text layer of a
            // two-layer scan without its mask would cover the whole page.
            std::optional<QImage> mask = PDFImageConversion::createAlphaMask(image);
            if (!mask)
            {
                // The mask cannot be created, so the transparency would be lost
                return QImage();
            }

            if (alphaMask)
            {
                *alphaMask = *mask;
            }
            return createFillImage(mask->isNull() ? QSize(1, 1) : mask->size(), mode == ItemMode::FillBlack);
        }

        case ItemMode::Original:
            return image;
    }

    Q_ASSERT(false);
    return QImage();
}

PDFObject PDFBitonalDocumentCreator::createConvertedImageObject(PDFObjectReference originalImage,
                                                                const QImage& bitonalImage,
                                                                const QImage& alphaMask,
                                                                Compression compression,
                                                                PDFDocumentBuilder* builder,
                                                                qint64* maskSize) const
{
    PDFObject imageObject = createBitonalImageObject(bitonalImage, compression);
    if (imageObject.isNull())
    {
        return PDFObject();
    }

    PDFDictionaryBuilder dictionary(*imageObject.getStream()->getDictionary());
    QByteArray content = *imageObject.getStream()->getContent();

    // Transfer the entries of the original image, which are not related
    // to the image samples, into the converted image.
    if (const PDFDictionary* originalDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(originalImage)))
    {
        for (const char* key : PRESERVED_IMAGE_DICTIONARY_KEYS)
        {
            if (originalDictionary->hasKey(key))
            {
                dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject(originalDictionary->get(key)));
            }
        }
    }

    // The original image can be transparent - it can have a soft mask, a stencil
    // mask, a color key mask, or an alpha channel stored directly in the image
    // data. All these variants are decoded into the alpha channel of the decoded
    // image, so a single soft mask created from that alpha channel replaces them.
    if (!alphaMask.isNull())
    {
        PDFObject softMaskObject = createBitonalImageObject(alphaMask, compression);

        if (!softMaskObject.isNull())
        {
            if (maskSize)
            {
                *maskSize = softMaskObject.getStream()->getContent()->size();
            }

            if (builder)
            {
                PDFObjectReference softMaskReference = builder->addObject(std::move(softMaskObject));
                dictionary.setEntry(PDFInplaceOrMemoryString("SMask"), PDFObject::createReference(softMaskReference));
            }
        }
    }

    return PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content)));
}

bool PDFBitonalDocumentCreator::createBitonalDocumentFromImages(PDFDocumentBuilder& builder, const Settings& settings, const PDFOperationControl* operationControl, std::set<PDFObjectReference>* replacedImages)
{
    // The settings are a public API, so they can contain the same image more than
    // once. The mode of the last occurrence wins - including the mode, which leaves
    // the image untouched, so a later item can cancel an earlier request. Only after
    // the duplicates are resolved, the untouched images are left out.
    std::map<PDFObjectReference, ItemMode> imageModes;
    std::vector<PDFObjectReference> imageOrder;

    for (const ItemInfo& item : settings.items)
    {
        if (!item.imageReference.isValid())
        {
            continue;
        }

        if (imageModes.insert_or_assign(item.imageReference, item.mode).second)
        {
            imageOrder.push_back(item.imageReference);
        }
    }

    std::vector<ItemInfo> itemsToBeConverted;

    for (const PDFObjectReference reference : imageOrder)
    {
        ItemInfo item;
        item.imageReference = reference;
        item.mode = imageModes.at(reference);

        if (item.isContentReplaced())
        {
            itemsToBeConverted.push_back(item);
        }
    }

    // Do we have something to be converted?
    if (itemsToBeConverted.empty())
    {
        return false;
    }

    startProgress(itemsToBeConverted.size(), PDFTranslationContext::tr("Converting images..."));

    // The progress must be finished even when an exception escapes from this function
    auto progressGuard = qScopeGuard([this]() { finishProgress(); });

    bool isConverted = false;

    for (const ItemInfo& item : itemsToBeConverted)
    {
        // B58: the conversion of the images stops between the images and inside of the
        // decoding and the thresholding of one
        if (PDFOperationControl::isOperationCanceled(operationControl))
        {
            return false;
        }

        const PDFObjectReference reference = item.imageReference;
        if (isImageTooLarge(reference))
        {
            // Plan 6 of step 4: never decoded, the image stays as it is and the result says why
            addFailedItem(item, FailureReason::TooLarge);
            stepProgress();
            continue;
        }

        QImage image = getDecodedImage(reference, operationControl);

        if (image.isNull())
        {
            // Image cannot be decoded, so it is left in the document as it is
            if (!PDFOperationControl::isOperationCanceled(operationControl))
            {
                addFailedItem(item, FailureReason::CannotBeRead);
            }
            stepProgress();
            continue;
        }

        QImage alphaMask;
        const QImage bitonalImage = createItemImage(image, ConversionSource::Images, item.mode, settings.conversionMethod, settings.manualThreshold, &alphaMask, nullptr, operationControl);

        if (PDFOperationControl::isOperationCanceled(operationControl))
        {
            return false;
        }

        // A null image here is an image, whose transparency cannot be kept by a fill, or
        // a conversion, which has failed: the image stays as it is
        PDFObject imageObject = bitonalImage.isNull() ? PDFObject() : createConvertedImageObject(reference, bitonalImage, alphaMask, settings.compression, &builder, nullptr);

        if (!imageObject.isNull())
        {
            addUsedCompression(imageObject);
            builder.setObject(reference, std::move(imageObject));
            replacedImages->insert(reference);
            isConverted = true;
            ++m_convertedItemCount;
        }
        else
        {
            addFailedItem(item, bitonalImage.isNull() ? FailureReason::CannotBeRead : FailureReason::CannotBeEncoded);
        }

        stepProgress();
    }

    return isConverted;
}

bool PDFBitonalDocumentCreator::createBitonalDocumentFromPages(PDFDocumentBuilder& builder, const Settings& settings, const PDFOperationControl* operationControl)
{
    const PDFCatalog* catalog = m_document->getCatalog();
    const size_t pageCount = catalog->getPageCount();

    // The settings are a public API, so they can contain an index of a page, which does
    // not exist, or the same page more than once. Both must be resolved before anything
    // is rasterized - a duplicated page in the algorithm mode would be rendered by two
    // tasks writing into the same slot at the same time, which is a data race, and an
    // index out of the range would access the vector of the images outside its bounds.
    // When a page is requested more than once, the mode of its last occurrence wins -
    // including the mode, which leaves the page untouched, so a later item can cancel
    // an earlier request. Only after the duplicates are resolved, the untouched pages
    // are left out.
    std::map<PDFInteger, ItemMode> pageModes;

    for (const ItemInfo& item : settings.items)
    {
        if (item.pageIndex >= 0 && size_t(item.pageIndex) < pageCount)
        {
            pageModes[item.pageIndex] = item.mode;
        }
    }

    std::erase_if(pageModes, [](const auto& entry) { return entry.second == ItemMode::Original; });

    // Do we have something to be converted?
    if (pageModes.empty())
    {
        return false;
    }

    // Only the pages converted by the algorithm have to be rasterized - a filled page
    // is built from a single sample, so it costs nothing.
    std::vector<PDFInteger> pageIndices;
    std::vector<PDFInteger> rasterizedPageIndices;
    std::set<PDFInteger> tooLargePages;
    pageIndices.reserve(pageModes.size());

    for (const auto& [pageIndex, mode] : pageModes)
    {
        pageIndices.push_back(pageIndex);

        if (isConversionMode(mode))
        {
            // B65: a page, whose image would exceed the limits of the pixels, is never
            // rasterized - it stays as it is and the result says why
            const PDFPage* page = catalog->getPage(size_t(pageIndex));
            if (page && !page->getMediaBox().isEmpty() && !getPageImageSize(page, settings.dpiResolution).isValid())
            {
                tooLargePages.insert(pageIndex);
                continue;
            }

            rasterizedPageIndices.push_back(pageIndex);
        }
    }

    if (!rasterizedPageIndices.empty() && !m_rasterizerPool)
    {
        // Pages cannot be rasterized without the pool
        return false;
    }

    // Rasterizing is the only expensive part of the conversion, so the progress counts
    // the rasterized pages. At least one step is needed even when all pages are only
    // filled, so the progress does not divide by a zero page count.
    startProgress(qMax<size_t>(rasterizedPageIndices.size(), 1), PDFTranslationContext::tr("Converting pages..."));

    // The progress must be finished even when an exception escapes from this function
    auto progressGuard = qScopeGuard([this]() { finishProgress(); });

    bool isConverted = false;
    const int dpiResolution = settings.dpiResolution;

    // Pages are rasterized and converted in parallel, but the converted images are
    // stored as encoded image streams only - the rasterized images are large and we
    // do not want to keep all of them in the memory at once.
    std::vector<PDFObject> imageObjects(pageCount);

    // Why a rasterized page has no image: its image could not be created (a null image), or
    // its bitonal image was refused by every encoder. Each page has its own element.
    std::vector<char> encodingFailures(pageCount, 0);

    // The problems of the rendering of each page (owner 14. 11. 2026: a rendering error never
    // blocks the conversion, the result only asks the user to check the page). Each page has
    // its own element.
    std::vector<QStringList> pageIssues(pageCount);

    auto pageSizeGetter = [dpiResolution](const PDFPage* page) { return getPageImageSize(page, dpiResolution); };
    auto pageImageProcessor = [this, &imageObjects, &encodingFailures, &pageIssues, &pageModes, &settings, operationControl](PDFInteger pageIndex, QImage image, const QList<PDFRenderError>& errors)
    {
        QStringList& issues = pageIssues[size_t(pageIndex)];
        for (const PDFRenderError& error : errors)
        {
            if (!issues.contains(error.message))
            {
                issues << error.message;
            }
        }

        // Only the rasterized pages reach this point, so the mode is one of the two
        // conversion ones. The map is read by several threads at once, which is safe.
        const QImage bitonalImage = createItemImage(image, ConversionSource::Pages, pageModes.at(pageIndex), settings.conversionMethod, settings.manualThreshold, nullptr, nullptr, operationControl);

        if (!bitonalImage.isNull())
        {
            imageObjects[size_t(pageIndex)] = createBitonalImageObject(bitonalImage, settings.compression);
            encodingFailures[size_t(pageIndex)] = imageObjects[size_t(pageIndex)].isNull() ? char(1) : char(0);
        }
        stepProgress();
    };

    renderPageImages(rasterizedPageIndices, pageSizeGetter, pageImageProcessor, operationControl);

    if (PDFOperationControl::isOperationCanceled(operationControl))
    {
        return false;
    }

    // Pages, whose content has really been replaced. A page, whose image could not be
    // created, keeps its original content, so it also keeps its marked content and
    // it must be counted as not converted.
    std::set<PDFObjectReference> convertedPages;

    for (const PDFInteger pageIndex : pageIndices)
    {
        const ItemMode mode = pageModes.at(pageIndex);
        PDFObject imageObject;

        ItemInfo item;
        item.pageIndex = pageIndex;
        item.mode = mode;

        if (tooLargePages.count(pageIndex))
        {
            addFailedItem(item, FailureReason::TooLarge);
            continue;
        }

        if (isConversionMode(mode))
        {
            imageObject = std::move(imageObjects[size_t(pageIndex)]);
        }
        else
        {
            // Page is covered by a solid area, which does not need any rasterization
            imageObject = createBitonalImageObject(createItemImage(QImage(1, 1, QImage::Format_RGB32), ConversionSource::Pages, mode, settings.conversionMethod, settings.manualThreshold, nullptr, nullptr, nullptr), settings.compression);
        }

        const PDFPage* page = catalog->getPage(size_t(pageIndex));

        if (!page || page->getMediaBox().isEmpty())
        {
            addFailedItem(item, FailureReason::InvalidPage);
            continue;
        }

        if (imageObject.isNull())
        {
            // The image of the page could not be created or encoded, so it stays as it is
            addFailedItem(item, encodingFailures[size_t(pageIndex)] ? FailureReason::CannotBeEncoded : FailureReason::RenderingErrors);
            continue;
        }

        const QRectF mediaBox = page->getMediaBox();
        const std::optional<Compression> pageCompression = getImageObjectCompression(imageObject);

        const PDFObjectReference imageReference = builder.addObject(std::move(imageObject));

        // Image is placed onto the whole media box of the page. The media box is
        // expressed in the coordinate system of the page, in which the y axis points
        // upwards, so the lower left corner of the box is (left(), top()).
        QByteArray contentStream = QString("q %1 0 0 %2 %3 %4 cm /BitonalImage Do Q")
                                       .arg(mediaBox.width(), 0, 'f', 6)
                                       .arg(mediaBox.height(), 0, 'f', 6)
                                       .arg(mediaBox.left(), 0, 'f', 6)
                                       .arg(mediaBox.top(), 0, 'f', 6).toLatin1();
        QByteArray compressedContentStream = PDFFlateDecodeFilter::compress(contentStream);

        PDFDictionaryBuilder contentStreamDictionary;
        contentStreamDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        contentStreamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedContentStream.size()));

        const PDFObjectReference contentStreamReference = builder.addObject(PDFObject::createStream(PDFStream(std::move(contentStreamDictionary), std::move(compressedContentStream))));

        PDFDictionaryBuilder xobjectDictionary;
        xobjectDictionary.setEntry(PDFInplaceOrMemoryString("BitonalImage"), PDFObject::createReference(imageReference));

        PDFArrayBuilder procSetArray;
        procSetArray.appendItem(PDFObject::createName("PDF"));
        procSetArray.appendItem(PDFObject::createName("ImageB"));

        PDFDictionaryBuilder resourcesDictionary;
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::move(xobjectDictionary)));
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("ProcSet"), PDFObject::createArray(std::move(procSetArray)));

        const PDFObjectReference pageReference = page->getPageReference();
        const PDFDictionary* originalPageDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(pageReference));

        if (!originalPageDictionary)
        {
            addFailedItem(item, FailureReason::InvalidPage);
            continue;
        }

        // Everything except the page content is preserved - the page keeps its size,
        // rotation, annotations and other properties.
        PDFDictionaryBuilder pageDictionary(*originalPageDictionary);
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contentStreamReference));
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::move(resourcesDictionary)));

        // The marked content of the page is gone, so the page must not be a part
        // of the structure tree anymore.
        pageDictionary.removeEntry("StructParents");

        // The embedded thumbnail shows the original, colored content of the page.
        // Keeping it would both display a wrong preview in the viewer and prevent
        // the removal of the unused objects it references.
        pageDictionary.removeEntry("Thumb");

        builder.setObject(pageReference, PDFObject::createDictionary(std::move(pageDictionary)));
        isConverted = true;
        convertedPages.insert(pageReference);
        if (pageCompression)
        {
            ++m_usedCompressions[*pageCompression];
        }
        ++m_convertedItemCount;

        // Converted, but the renderer has reported problems: the user is asked to check it
        if (isConversionMode(mode) && !pageIssues[size_t(pageIndex)].isEmpty())
        {
            m_renderingIssues.push_back({ pageIndex, std::move(pageIssues[size_t(pageIndex)]) });
        }
    }

    if (convertedPages.size() == pageCount)
    {
        // Every page of the document has really got a new content stream, so no page
        // is tagged anymore and the whole structure tree can be removed
        removeStructureTree(builder);
    }
    else if (!convertedPages.empty())
    {
        // The other pages keep their marked content, so the structure tree must
        // survive, but it must not refer to the content, which is gone
        pruneStructureTree(builder, convertedPages);
    }

    return isConverted;
}

void PDFBitonalDocumentCreator::removeStructureTree(PDFDocumentBuilder& builder) const
{
    const PDFObjectReference catalogReference = builder.getCatalogReference();

    if (const PDFDictionary* originalCatalogDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(catalogReference)))
    {
        if (!originalCatalogDictionary->hasKey("StructTreeRoot") && !originalCatalogDictionary->hasKey("MarkInfo"))
        {
            return;
        }

        PDFDictionaryBuilder catalogDictionary(*originalCatalogDictionary);
        catalogDictionary.removeEntry("StructTreeRoot");
        catalogDictionary.removeEntry("MarkInfo");
        builder.setObject(catalogReference, PDFObject::createDictionary(std::move(catalogDictionary)));
    }
}

/// Removes the references to the content of the converted pages from the structure
/// tree of a document. The content of a converted page is a single image, so the
/// marked content sequences of the page do not exist anymore and the structure
/// elements must not refer to them - a viewer would look up the marked content
/// identifiers in a content stream, which does not contain them, and an extraction
/// of the text would use the actual text of an element, whose content is gone.
///
/// The pruning walks the tree from its root. A marked content reference (an integer
/// identifier or a MCR dictionary) lying on a converted page is removed, an object
/// reference lying on a converted page is removed unless it refers to an annotation
/// of the page (annotations are kept alive by the conversion), and an element, which
/// has lost all its kids, is removed as well. The parent tree loses the entries of
/// the converted pages and the entries pointing to the removed elements, the ID tree
/// loses the removed elements and the Ref entries of the kept elements do not point
/// to the removed elements anymore.
class PDFStructureTreePruner
{
public:
    explicit PDFStructureTreePruner(const PDFDocument* document,
                                    PDFDocumentBuilder* builder,
                                    const std::set<PDFObjectReference>& convertedPages) :
        m_document(document),
        m_builder(builder),
        m_convertedPages(convertedPages),
        m_loader(document)
    {

    }

    /// Prunes the tree. Returns false, if no element remains in the tree, so the
    /// tree should be removed altogether.
    bool prune();

private:
    /// Result of the pruning of an element dictionary
    struct ElementResult
    {
        bool isKept = true;
        bool isChanged = false;
        PDFDictionaryBuilder dictionary;
    };

    /// Result of the pruning of the kids of an element
    struct KidsResult
    {
        bool isChanged = false;
        PDFObject kids;
    };

    KidsResult pruneKids(const PDFObject& kids, PDFObjectReference page);
    std::optional<PDFObject> pruneKid(const PDFObject& kid, PDFObjectReference page);
    ElementResult pruneElementDictionary(const PDFDictionary* dictionary, PDFObjectReference inheritedPage);
    bool pruneElement(PDFObjectReference reference, PDFObjectReference inheritedPage);
    PDFObject rebuildParentTree(const PDFObject& parentTree, const std::set<PDFInteger>& removedKeys);
    PDFObject rebuildIdTree(const PDFObject& idTree);
    void pruneElementReferences();

    bool isPageConverted(PDFObjectReference page) const { return m_convertedPages.count(page) > 0; }
    bool isElementRemoved(const PDFObject& object) const { return object.isReference() && m_removedElements.count(object.getReference()) > 0; }
    bool isAnnotationOfPage(PDFObjectReference page, PDFObjectReference object) const;

    /// Returns the effective page of a structure item - its own page, or the page
    /// inherited from its parent element, when it has none
    PDFObjectReference getEffectivePage(const PDFDictionary* dictionary, PDFObjectReference inheritedPage) const;

    /// Writes the object into the builder, either as the indirect object or as
    /// a direct entry of the parent dictionary
    static void setObject(PDFDocumentBuilder* builder, const PDFObject& original, PDFDictionaryBuilder& parentDictionary, const char* key, PDFObject object);

    const PDFDocument* m_document;
    PDFDocumentBuilder* m_builder;
    const std::set<PDFObjectReference>& m_convertedPages;
    PDFDocumentDataLoaderDecorator m_loader;

    /// Elements, which have already been visited, and whether they have been kept
    std::map<PDFObjectReference, bool> m_visitedElements;

    /// Elements, which have been removed from the tree
    std::set<PDFObjectReference> m_removedElements;

    /// Kept elements, which refer to other elements by the Ref entry
    std::vector<PDFObjectReference> m_elementsWithReferences;
};

bool PDFStructureTreePruner::prune()
{
    const PDFObjectReference catalogReference = m_builder->getCatalogReference();
    const PDFDictionary* catalogDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(catalogReference));

    if (!catalogDictionary)
    {
        return true;
    }

    const PDFObject rootObject = catalogDictionary->get("StructTreeRoot");
    const PDFDictionary* rootDictionary = m_document->getDictionaryFromObject(rootObject);

    if (!rootDictionary)
    {
        return true;
    }

    // The keys of the parent tree, which belong to the converted pages, are gone
    // together with the marked content of the pages
    std::set<PDFInteger> removedParentTreeKeys;

    for (const PDFObjectReference pageReference : m_convertedPages)
    {
        if (const PDFDictionary* pageDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(pageReference)))
        {
            const PDFObject structParents = m_document->getObject(pageDictionary->get("StructParents"));

            if (structParents.isInt())
            {
                removedParentTreeKeys.insert(structParents.getInteger());
            }
        }
    }

    const PDFObject originalKids = rootDictionary->get("K");
    KidsResult kids = pruneKids(originalKids, PDFObjectReference());

    if (kids.isChanged && kids.kids.isNull())
    {
        // Every element of the tree has been removed
        return false;
    }

    pruneElementReferences();

    PDFDictionaryBuilder newRootDictionary(*rootDictionary);
    bool isRootChanged = false;

    if (kids.isChanged)
    {
        newRootDictionary.setEntry(PDFInplaceOrMemoryString("K"), std::move(kids.kids));
        isRootChanged = true;
    }

    if (rootDictionary->hasKey("ParentTree"))
    {
        const PDFObject originalParentTree = rootDictionary->get("ParentTree");
        PDFObject parentTree = rebuildParentTree(originalParentTree, removedParentTreeKeys);

        if (!parentTree.isNull())
        {
            setObject(m_builder, originalParentTree, newRootDictionary, "ParentTree", std::move(parentTree));
            isRootChanged = true;
        }
    }

    if (rootDictionary->hasKey("IDTree"))
    {
        const PDFObject originalIdTree = rootDictionary->get("IDTree");
        PDFObject idTree = rebuildIdTree(originalIdTree);

        if (!idTree.isNull())
        {
            setObject(m_builder, originalIdTree, newRootDictionary, "IDTree", std::move(idTree));
            isRootChanged = true;
        }
    }

    if (isRootChanged)
    {
        if (rootObject.isReference())
        {
            m_builder->setObject(rootObject.getReference(), PDFObject::createDictionary(std::move(newRootDictionary)));
        }
        else
        {
            PDFDictionaryBuilder newCatalogDictionary(*catalogDictionary);
            newCatalogDictionary.setEntry(PDFInplaceOrMemoryString("StructTreeRoot"), PDFObject::createDictionary(std::move(newRootDictionary)));
            m_builder->setObject(catalogReference, PDFObject::createDictionary(std::move(newCatalogDictionary)));
        }
    }

    return true;
}

PDFStructureTreePruner::KidsResult PDFStructureTreePruner::pruneKids(const PDFObject& kids, PDFObjectReference page)
{
    KidsResult result;
    result.kids = kids;

    const PDFObject dereferencedKids = m_document->getObject(kids);

    if (dereferencedKids.isArray())
    {
        PDFArrayBuilder newKids;
        const PDFArray* kidsArray = dereferencedKids.getArray();

        for (size_t i = 0, count = kidsArray->getCount(); i < count; ++i)
        {
            const PDFObject& kid = kidsArray->getItem(i);
            std::optional<PDFObject> newKid = pruneKid(kid, page);

            if (!newKid)
            {
                result.isChanged = true;
                continue;
            }

            if (!(*newKid == kid))
            {
                result.isChanged = true;
            }

            newKids.appendItem(std::move(*newKid));
        }

        if (result.isChanged)
        {
            result.kids = newKids.getCount() > 0 ? PDFObject::createArray(std::move(newKids)) : PDFObject();
        }
    }
    else if (!kids.isNull())
    {
        std::optional<PDFObject> newKid = pruneKid(kids, page);

        if (!newKid)
        {
            result.isChanged = true;
            result.kids = PDFObject();
        }
        else if (!(*newKid == kids))
        {
            result.isChanged = true;
            result.kids = std::move(*newKid);
        }
    }

    return result;
}

std::optional<PDFObject> PDFStructureTreePruner::pruneKid(const PDFObject& kid, PDFObjectReference page)
{
    if (kid.isInt())
    {
        // A marked content identifier of the page of the element
        if (isPageConverted(page))
        {
            return std::nullopt;
        }

        return kid;
    }

    if (kid.isArray())
    {
        // A nested array of kids is not standard, but it is handled the same way
        KidsResult result = pruneKids(kid, page);

        if (result.isChanged && result.kids.isNull())
        {
            return std::nullopt;
        }

        return result.kids;
    }

    const PDFDictionary* dictionary = m_document->getDictionaryFromObject(kid);

    if (!dictionary)
    {
        // Something, which is not understood, is left as it is
        return kid;
    }

    const QByteArray type = m_loader.readNameFromDictionary(dictionary, "Type");

    if (type == "MCR")
    {
        // A marked content sequence of the page of the reference
        if (isPageConverted(getEffectivePage(dictionary, page)))
        {
            return std::nullopt;
        }

        return kid;
    }

    if (type == "OBJR")
    {
        // An object of the page - the annotations of the converted page are kept
        // alive by the conversion, everything else of the page is gone
        const PDFObjectReference objectPage = getEffectivePage(dictionary, page);

        if (isPageConverted(objectPage) && !isAnnotationOfPage(objectPage, m_loader.readReferenceFromDictionary(dictionary, "Obj")))
        {
            return std::nullopt;
        }

        return kid;
    }

    // A structure element
    if (kid.isReference())
    {
        if (!pruneElement(kid.getReference(), page))
        {
            return std::nullopt;
        }

        return kid;
    }

    ElementResult result = pruneElementDictionary(dictionary, page);

    if (!result.isKept)
    {
        return std::nullopt;
    }

    if (result.isChanged)
    {
        return PDFObject::createDictionary(std::move(result.dictionary));
    }

    return kid;
}

PDFStructureTreePruner::ElementResult PDFStructureTreePruner::pruneElementDictionary(const PDFDictionary* dictionary, PDFObjectReference inheritedPage)
{
    ElementResult result;

    const PDFObjectReference page = getEffectivePage(dictionary, inheritedPage);
    const PDFObject originalKids = dictionary->get("K");
    KidsResult kids = pruneKids(originalKids, page);

    if (!kids.isChanged)
    {
        return result;
    }

    if (kids.kids.isNull())
    {
        // The element has lost all its content
        result.isKept = false;
        return result;
    }

    result.isChanged = true;
    result.dictionary = PDFDictionaryBuilder(*dictionary);
    result.dictionary.setEntry(PDFInplaceOrMemoryString("K"), std::move(kids.kids));
    return result;
}

bool PDFStructureTreePruner::pruneElement(PDFObjectReference reference, PDFObjectReference inheritedPage)
{
    auto it = m_visitedElements.find(reference);

    if (it != m_visitedElements.cend())
    {
        // The element has already been processed, or it is being processed - a damaged
        // document can contain a cycle, so an element being processed is kept
        return it->second;
    }

    m_visitedElements[reference] = true;

    const PDFDictionary* dictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference));

    if (!dictionary)
    {
        return true;
    }

    ElementResult result = pruneElementDictionary(dictionary, inheritedPage);

    if (!result.isKept)
    {
        m_visitedElements[reference] = false;
        m_removedElements.insert(reference);
        return false;
    }

    if (result.isChanged)
    {
        m_builder->setObject(reference, PDFObject::createDictionary(std::move(result.dictionary)));
    }

    if (dictionary->hasKey("Ref"))
    {
        m_elementsWithReferences.push_back(reference);
    }

    return true;
}

PDFObject PDFStructureTreePruner::rebuildParentTree(const PDFObject& parentTree, const std::set<PDFInteger>& removedKeys)
{
    struct Entry
    {
        PDFInteger key = 0;
        PDFObject value;

        bool operator<(const Entry& other) const { return key < other.key; }

        static Entry parse(PDFInteger key, const PDFObjectStorage*, const PDFObject& object)
        {
            return Entry{ key, object };
        }
    };

    std::vector<Entry> entries = PDFNumberTreeLoader<Entry>::parse(&m_document->getStorage(), parentTree);
    bool isChanged = false;

    PDFArrayBuilder numbers;

    for (Entry& entry : entries)
    {
        if (removedKeys.count(entry.key) > 0)
        {
            // The marked content of the page is gone
            isChanged = true;
            continue;
        }

        if (isElementRemoved(entry.value))
        {
            // The element, which the object belongs to, is gone
            isChanged = true;
            continue;
        }

        const PDFObject dereferencedValue = m_document->getObject(entry.value);

        if (dereferencedValue.isArray())
        {
            // The array maps the marked content identifiers of a page to the elements.
            // The page is not converted, so its elements are kept - unless the document
            // is inconsistent. A removed element is replaced by null, so the identifiers
            // of the other elements keep their positions.
            const PDFArray* array = dereferencedValue.getArray();
            PDFArrayBuilder newArray;
            bool isArrayChanged = false;

            for (size_t i = 0, count = array->getCount(); i < count; ++i)
            {
                const PDFObject& item = array->getItem(i);

                if (isElementRemoved(item))
                {
                    newArray.appendItem(PDFObject());
                    isArrayChanged = true;
                }
                else
                {
                    newArray.appendItem(item);
                }
            }

            if (isArrayChanged)
            {
                PDFObject newArrayObject = PDFObject::createArray(std::move(newArray));

                if (entry.value.isReference())
                {
                    m_builder->setObject(entry.value.getReference(), std::move(newArrayObject));
                }
                else
                {
                    entry.value = std::move(newArrayObject);
                    isChanged = true;
                }
            }
        }

        numbers.appendItem(PDFObject::createInteger(entry.key));
        numbers.appendItem(entry.value);
    }

    if (!isChanged)
    {
        return PDFObject();
    }

    // The tree is rebuilt as a single node - that is a valid number tree of any size
    PDFDictionaryBuilder dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Nums"), PDFObject::createArray(std::move(numbers)));
    return PDFObject::createDictionary(std::move(dictionary));
}

PDFObject PDFStructureTreePruner::rebuildIdTree(const PDFObject& idTree)
{
    const std::map<QByteArray, PDFObject> entries = PDFNameTreeLoader<PDFObject>::parse(&m_document->getStorage(), idTree, [](const PDFObjectStorage*, const PDFObject& object) { return object; });
    bool isChanged = false;

    PDFArrayBuilder names;

    for (const auto& [name, value] : entries)
    {
        if (isElementRemoved(value))
        {
            isChanged = true;
            continue;
        }

        names.appendItem(PDFObject::createString(name));
        names.appendItem(value);
    }

    if (!isChanged)
    {
        return PDFObject();
    }

    // The tree is rebuilt as a single node with the keys in the byte order, in which
    // the map keeps them
    PDFDictionaryBuilder dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Names"), PDFObject::createArray(std::move(names)));
    return PDFObject::createDictionary(std::move(dictionary));
}

void PDFStructureTreePruner::pruneElementReferences()
{
    for (const PDFObjectReference reference : m_elementsWithReferences)
    {
        // The element can already be rewritten by the pruning of its kids, so the
        // current dictionary is taken from the builder
        const PDFObject elementObject = m_builder->getObjectByReference(reference);
        const PDFDictionary* dictionary = elementObject.isDictionary() ? elementObject.getDictionary() : nullptr;

        if (!dictionary)
        {
            continue;
        }

        const PDFObject references = m_document->getObject(dictionary->get("Ref"));

        if (!references.isArray())
        {
            continue;
        }

        PDFArrayBuilder newReferences;
        bool isChanged = false;

        for (size_t i = 0, count = references.getArray()->getCount(); i < count; ++i)
        {
            const PDFObject& item = references.getArray()->getItem(i);

            if (isElementRemoved(item))
            {
                isChanged = true;
                continue;
            }

            newReferences.appendItem(item);
        }

        if (!isChanged)
        {
            continue;
        }

        PDFDictionaryBuilder newDictionary(*dictionary);

        if (newReferences.getCount() > 0)
        {
            newDictionary.setEntry(PDFInplaceOrMemoryString("Ref"), PDFObject::createArray(std::move(newReferences)));
        }
        else
        {
            newDictionary.removeEntry("Ref");
        }

        m_builder->setObject(reference, PDFObject::createDictionary(std::move(newDictionary)));
    }
}

bool PDFStructureTreePruner::isAnnotationOfPage(PDFObjectReference page, PDFObjectReference object) const
{
    if (!object.isValid())
    {
        return false;
    }

    const PDFCatalog* catalog = m_document->getCatalog();
    const size_t pageIndex = catalog->getPageIndexFromPageReference(page);

    if (pageIndex >= catalog->getPageCount())
    {
        return false;
    }

    const PDFPage* pageObject = catalog->getPage(pageIndex);

    if (!pageObject)
    {
        return false;
    }

    const std::vector<PDFObjectReference>& annotations = pageObject->getAnnotations();
    return std::find(annotations.cbegin(), annotations.cend(), object) != annotations.cend();
}

PDFObjectReference PDFStructureTreePruner::getEffectivePage(const PDFDictionary* dictionary, PDFObjectReference inheritedPage) const
{
    const PDFObjectReference page = m_loader.readReferenceFromDictionary(dictionary, "Pg");
    return page.isValid() ? page : inheritedPage;
}

void PDFStructureTreePruner::setObject(PDFDocumentBuilder* builder, const PDFObject& original, PDFDictionaryBuilder& parentDictionary, const char* key, PDFObject object)
{
    if (original.isReference())
    {
        // The original object is indirect, so it is replaced in place and the parent
        // dictionary keeps referring to it
        builder->setObject(original.getReference(), std::move(object));
    }
    else
    {
        parentDictionary.setEntry(PDFInplaceOrMemoryString(key), std::move(object));
    }
}

void PDFBitonalDocumentCreator::pruneStructureTree(PDFDocumentBuilder& builder, const std::set<PDFObjectReference>& convertedPages) const
{
    PDFStructureTreePruner pruner(m_document, &builder, convertedPages);

    if (!pruner.prune())
    {
        // Nothing has remained in the tree
        removeStructureTree(builder);
    }
}

void PDFBitonalDocumentCreator::traversePageImages(const PDFPage* page,
                                                   const std::function<void(PDFObjectReference, const PDFDictionary*)>& imageProcessor) const
{
    if (!page)
    {
        return;
    }

    PDFDocumentDataLoaderDecorator loader(m_document);

    // Resources are an inheritable attribute of the page tree, so they can be stored
    // in a parent node of the page - PDFPage resolves the inheritance for us. Images
    // can also be hidden inside a form XObject, which has resources of its own, so
    // the forms are entered as well.
    std::set<PDFObjectReference> visitedForms;
    std::vector<PDFObject> resourcesToBeProcessed;
    resourcesToBeProcessed.push_back(page->getResources());

    while (!resourcesToBeProcessed.empty())
    {
        const PDFObject resources = resourcesToBeProcessed.back();
        resourcesToBeProcessed.pop_back();

        const PDFDictionary* resourcesDictionary = m_document->getDictionaryFromObject(resources);
        if (!resourcesDictionary)
        {
            continue;
        }

        const PDFDictionary* xobjectDictionary = m_document->getDictionaryFromObject(resourcesDictionary->get("XObject"));
        if (!xobjectDictionary)
        {
            continue;
        }

        for (size_t index = 0, count = xobjectDictionary->getCount(); index < count; ++index)
        {
            const PDFObject& item = xobjectDictionary->getValue(index);

            // An XObject is always a stream, so it is always an indirect object
            if (!item.isReference())
            {
                continue;
            }

            const PDFObjectReference reference = item.getReference();
            const PDFDictionary* itemDictionary = m_document->getDictionaryFromObject(item);

            if (!itemDictionary)
            {
                continue;
            }

            const QByteArray subtype = loader.readNameFromDictionary(itemDictionary, "Subtype");

            if (subtype == "Image")
            {
                imageProcessor(reference, itemDictionary);
            }
            else if (subtype == "Form" && visitedForms.insert(reference).second)
            {
                // A damaged document can contain a form referencing itself, so every
                // form is entered only once.
                resourcesToBeProcessed.push_back(itemDictionary->get("Resources"));
            }
        }
    }
}

std::vector<PDFObjectReference> PDFBitonalDocumentCreator::getConvertibleImages() const
{
    std::vector<PDFObjectReference> references;

    // The images drawn by the content in the order of their first drawing (the list, which a
    // user interface builds from them, follows the document); the same image on several pages
    // is one item. The images of every layer are converted (R4-4).
    const PDFDocumentImageList imageList = PDFDocumentImageList::create(m_document, PDFDocumentImageList::Options::createForChange(), nullptr, nullptr);
    for (const PDFDocumentImageList::Item& item : imageList.getItems())
    {
        if (isConvertibleImage(item))
        {
            references.push_back(item.key.reference);
        }
    }

    return references;
}

bool PDFBitonalDocumentCreator::isConvertibleImage(const PDFDocumentImageList::Item& item)
{
    // Inline images are kept (R4-8); stencil masks are already bitonal and they are
    // painted using the current fill color; a mask of another image would change the
    // transparency of that image; an image of an appearance of an annotation too would
    // change the appearance (R4-4)
    return item.kind == PDFDocumentImageList::Kind::XObject && !item.isImageMask && !item.isUsedAsMask && !item.isInAppearance && item.key.reference.isValid();
}

QString PDFBitonalDocumentCreator::getPageImageSizeError(const PDFPage* page, PDFInteger pageNumber, int dpiResolution)
{
    if (!page)
    {
        return QString();
    }

    const int resolution = qBound(MINIMUM_DPI_RESOLUTION, dpiResolution, MAXIMUM_DPI_RESOLUTION);
    const QSizeF size = getPageSizeInInches(page) * resolution;
    if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() <= 0.0 || size.height() <= 0.0)
    {
        // A degenerate page is reported by the conversion itself (it stays as it is)
        return QString();
    }

    const QLocale locale;
    if (size.height() > MAXIMUM_PAGE_IMAGE_DIMENSION)
    {
        return PDFTranslationContext::tr("Page %1 would be %2 px high – lower the resolution.").arg(pageNumber).arg(locale.toString(qint64(std::ceil(size.height()))));
    }
    if (size.width() > MAXIMUM_PAGE_IMAGE_DIMENSION)
    {
        return PDFTranslationContext::tr("Page %1 would be %2 px wide – lower the resolution.").arg(pageNumber).arg(locale.toString(qint64(std::ceil(size.width()))));
    }
    if (std::ceil(size.width()) * std::ceil(size.height()) * double(PDFRasterizerPool::BYTES_PER_PIXEL) > double(MAXIMUM_PAGE_IMAGE_BYTES))
    {
        return PDFTranslationContext::tr("Page %1 would need more than 2 GB of image data – lower the resolution.").arg(pageNumber);
    }
    return QString();
}

QImage PDFBitonalDocumentCreator::convertImageToBitonal(const QImage& image,
                                                        PDFImageConversion::ConversionMethod conversionMethod,
                                                        int threshold,
                                                        QImage* alphaMask,
                                                        const PDFOperationControl* operationControl,
                                                        int* usedThreshold)
{
    if (image.isNull())
    {
        return QImage();
    }

    PDFImageConversion imageConversion;
    imageConversion.setConversionMethod(conversionMethod);
    imageConversion.setThreshold(threshold);
    imageConversion.setAlphaMode(PDFImageConversion::AlphaMode::Composite);
    imageConversion.setOperationControl(operationControl);
    imageConversion.setImage(image);

    if (!imageConversion.convert())
    {
        return QImage();
    }

    if (alphaMask)
    {
        *alphaMask = imageConversion.getConvertedAlphaMask();
    }

    if (usedThreshold)
    {
        *usedThreshold = imageConversion.getThreshold();
    }

    return imageConversion.getConvertedImage();
}

PDFObject PDFBitonalDocumentCreator::createBitonalImageObject(const QImage& image, Compression compression)
{
    if (image.isNull())
    {
        return PDFObject();
    }

    auto encode = [&image](PDFImage::ImageCompression imageCompression) -> PDFObject
    {
        try
        {
            PDFImage::ImageEncodeOptions options;
            options.compression = imageCompression;
            options.colorMode = PDFImage::ImageColorMode::Monochrome;

            PDFStream stream = PDFImage::createStreamFromImage(image, options, nullptr);

            PDFDictionaryBuilder dictionary(*stream.getDictionary());
            QByteArray content = *stream.getContent();

            return PDFObject::createStream(PDFStream(std::move(dictionary), std::move(content)));
        }
        catch (const PDFException&)
        {
            // An encoder can refuse the image - the bitonal codings have their own
            // limits of the image size, for example. Such an algorithm is skipped.
            return PDFObject();
        }
    };

    // The candidates are ordered from the most widely supported coding to the least
    // one, so a tie is decided in favor of the coding, which every reader can read.
    // The Flate coding leads the list also because it accepts every image, so the
    // automatic mode always produces a result.
    constexpr std::array<std::pair<Compression, PDFImage::ImageCompression>, 4> codings = {
        std::pair{ Compression::Flate, PDFImage::ImageCompression::Flate },
        std::pair{ Compression::RunLength, PDFImage::ImageCompression::RunLength },
        std::pair{ Compression::CCITTGroup4, PDFImage::ImageCompression::CCITTGroup4 },
        std::pair{ Compression::JBIG2, PDFImage::ImageCompression::JBIG2 }
    };

    PDFObject result;
    int resultSize = 0;

    for (const auto& [coding, imageCompression] : codings)
    {
        if (compression != Compression::Auto && compression != coding)
        {
            continue;
        }

        PDFObject candidate = encode(imageCompression);

        if (candidate.isNull())
        {
            continue;
        }

        // The images differ by their data, the dictionaries are nearly the same - only
        // the filter and its parameters differ - so the size of the data decides.
        const int candidateSize = candidate.getStream()->getContent()->size();

        if (result.isNull() || candidateSize < resultSize)
        {
            result = std::move(candidate);
            resultSize = candidateSize;
        }
    }

    return result;
}

QImage PDFBitonalDocumentCreator::invertBitonalImage(QImage image)
{
    if (image.isNull() || image.format() != QImage::Format_Mono)
    {
        return QImage();
    }

    // A bitonal image stores a single bit per pixel and the default color table of the
    // format Format_Mono maps the sample value 0 to the black color and the value 1 to
    // the white one, so inverting the samples swaps the two colors. The bits, which pad
    // a row to the whole byte, are inverted as well, but they lie outside of the image
    // and neither the encoders nor the readers of the document ever see them.
    image.invertPixels();
    return image;
}

PDFBitonalDocumentCreator::BlankPageInfo PDFBitonalDocumentCreator::detectBlankPage(const QImage& pageImage,
                                                                                   int dpiResolution,
                                                                                   const PDFOperationControl* operationControl)
{
    if (pageImage.isNull() || dpiResolution <= 0 || PDFOperationControl::isOperationCanceled(operationControl))
    {
        return BlankPageInfo();
    }

    // The page is thresholded by the automatic method deliberately, so the answer
    // does not depend on the settings of the conversion. The manual threshold is
    // not used by that method, so the value passed here is irrelevant.
    const QImage bitonalImage = convertImageToBitonal(pageImage, PDFImageConversion::ConversionMethod::Automatic, 128, nullptr, operationControl);
    return analyzeBitonalPage(bitonalImage, dpiResolution, operationControl);
}

PDFBitonalDocumentCreator::BlankPageInfo PDFBitonalDocumentCreator::analyzeBitonalPage(const QImage& image,
                                                                                      int dpiResolution,
                                                                                      const PDFOperationControl* operationControl)
{
    BlankPageInfo result;

    if (image.isNull() || image.format() != QImage::Format_Mono || dpiResolution <= 0)
    {
        return result;
    }

    auto toPixels = [dpiResolution](double millimeters) -> int
    {
        return qMax(1, qRound(millimeters * dpiResolution / 25.4));
    };

    // A sample of the format Format_Mono is stored with the most significant bit
    // first and the default color table maps the value 0 to the black color
    auto isBlackSample = [](const uchar* line, int x) -> bool
    {
        return (line[x >> 3] & uchar(0x80 >> (x & 7))) == 0;
    };

    const int borderSize = toPixels(BLANK_PAGE_BORDER_MM);
    QRect area = image.rect().adjusted(borderSize, borderSize, -borderSize, -borderSize);

    if (area.isEmpty())
    {
        // The page is not larger than the border, so nothing can be left out of it
        area = image.rect();
    }

    // Count the black pixels first. A page, which carries more ink than a blank one
    // ever could, is decided by this pass alone - a page of a text consists of
    // hundreds of thousands of spots and grouping them would be a waste of time.
    const uint64_t areaPixelCount = uint64_t(area.width()) * uint64_t(area.height());
    const uint64_t inkPixelLimit = uint64_t(double(areaPixelCount) * BLANK_PAGE_MAXIMUM_INK_RATIO);

    uint64_t inkPixelCount = 0;
    bool isInkPixelLimitExceeded = false;

    for (int y = area.top(); y <= area.bottom() && !isInkPixelLimitExceeded; ++y)
    {
        if (PDFOperationControl::isOperationCanceled(operationControl))
        {
            return result;
        }

        const uchar* line = image.constScanLine(y);

        for (int x = area.left(); x <= area.right(); ++x)
        {
            if (isBlackSample(line, x) && ++inkPixelCount > inkPixelLimit)
            {
                isInkPixelLimitExceeded = true;
                break;
            }
        }
    }

    result.inkPixelCount = inkPixelCount;
    result.inkRatio = areaPixelCount > 0 ? double(inkPixelCount) / double(areaPixelCount) : 0.0;

    if (isInkPixelLimitExceeded || PDFOperationControl::isOperationCanceled(operationControl))
    {
        return result;
    }

    // Group the black pixels into connected spots. The pixels are grouped as runs of
    // a row, not one by one - a spot of dust is a run or two, so there are orders of
    // magnitude fewer runs than pixels.
    struct BlackRun
    {
        int y = 0;
        int xBegin = 0; ///< Position of the first black pixel of the run
        int xEnd = 0;   ///< Position after the last black pixel of the run
    };

    std::vector<BlackRun> runs;
    std::vector<size_t> parents;

    auto findRoot = [&parents](size_t index)
    {
        while (parents[index] != index)
        {
            parents[index] = parents[parents[index]];
            index = parents[index];
        }

        return index;
    };

    auto mergeRuns = [&findRoot, &parents](size_t first, size_t second)
    {
        const size_t firstRoot = findRoot(first);
        const size_t secondRoot = findRoot(second);

        if (firstRoot != secondRoot)
        {
            parents[qMax(firstRoot, secondRoot)] = qMin(firstRoot, secondRoot);
        }
    };

    size_t previousRowBegin = 0;
    size_t previousRowEnd = 0;

    for (int y = area.top(); y <= area.bottom(); ++y)
    {
        if (PDFOperationControl::isOperationCanceled(operationControl))
        {
            return result;
        }

        const uchar* line = image.constScanLine(y);
        const size_t currentRowBegin = runs.size();
        size_t previousIndex = previousRowBegin;

        int x = area.left();
        while (x <= area.right())
        {
            if (!isBlackSample(line, x))
            {
                ++x;
                continue;
            }

            const int xBegin = x;
            while (x <= area.right() && isBlackSample(line, x))
            {
                ++x;
            }

            const size_t runIndex = runs.size();
            runs.push_back(BlackRun{ y, xBegin, x });
            parents.push_back(runIndex);

            // The runs of both rows are ordered by their position, so a run of the
            // previous row, which cannot reach this one anymore, is skipped once and
            // for all. Diagonally adjacent pixels belong to the same spot, so two
            // runs meet, when their ranges overlap by a single pixel.
            while (previousIndex < previousRowEnd && runs[previousIndex].xEnd < xBegin)
            {
                ++previousIndex;
            }

            for (size_t index = previousIndex; index < previousRowEnd && runs[index].xBegin <= x; ++index)
            {
                mergeRuns(index, runIndex);
            }
        }

        previousRowBegin = currentRowBegin;
        previousRowEnd = runs.size();
    }

    // Bounds of the spots. Only the runs, which represent their spot, get them - the
    // bounds of the other ones stay null and they are skipped by the classification.
    std::vector<QRect> spots(runs.size());

    for (size_t index = 0; index < runs.size(); ++index)
    {
        if ((index & 0xFFFF) == 0 && PDFOperationControl::isOperationCanceled(operationControl))
        {
            return BlankPageInfo();
        }

        const BlackRun& run = runs[index];
        const QRect runBounds(run.xBegin, run.y, run.xEnd - run.xBegin, 1);

        QRect& bounds = spots[findRoot(index)];
        bounds = bounds.isNull() ? runBounds : bounds.united(runBounds);
    }

    const int contentSize = toPixels(BLANK_PAGE_CONTENT_SIZE_MM);
    const int lineLength = toPixels(BLANK_PAGE_LINE_LENGTH_MM);
    const int streakThickness = toPixels(BLANK_PAGE_STREAK_THICKNESS_MM);
    const int streakWidth = qRound(area.width() * BLANK_PAGE_STREAK_LENGTH_RATIO);
    const int streakHeight = qRound(area.height() * BLANK_PAGE_STREAK_LENGTH_RATIO);

    for (const QRect& bounds : spots)
    {
        if (PDFOperationControl::isOperationCanceled(operationControl))
        {
            return BlankPageInfo();
        }

        if (bounds.isNull())
        {
            continue;
        }

        const int width = bounds.width();
        const int height = bounds.height();

        // A spot of the size of a character, or a larger one, is a content
        if (width >= contentSize && height >= contentSize)
        {
            ++result.contentComponentCount;
            continue;
        }

        // A long thin spot is either a printed line - a rule of a table, an
        // underline - or a defect of the scanner: a hair, a scratch, a streak of a
        // dirty sensor. A streak runs across the whole page, a printed line does not.
        if (width >= lineLength || height >= lineLength)
        {
            const bool isStreak = (height <= streakThickness && width >= streakWidth) ||
                                  (width <= streakThickness && height >= streakHeight);

            if (!isStreak)
            {
                ++result.contentComponentCount;
            }

            continue;
        }

        // Anything else is a speck of the dust, of the toner or of the paper itself
    }

    result.isBlank = result.contentComponentCount == 0 && !PDFOperationControl::isOperationCanceled(operationControl);
    return result;
}

QImage PDFBitonalDocumentCreator::createFillImage(QSize size, bool isBlack)
{
    QImage image(size.expandedTo(QSize(1, 1)), QImage::Format_Mono);

    // The default color table of the format Format_Mono maps the sample value 0
    // to the black color and the sample value 1 to the white color.
    image.fill(isBlack ? 0 : 1);

    return image;
}

QImage PDFBitonalDocumentCreator::createFillPreviewImage(const QImage& image, bool isBlack)
{
    if (image.isNull())
    {
        return QImage();
    }

    std::optional<QImage> mask = PDFImageConversion::createAlphaMask(image);

    if (!mask)
    {
        // The mask cannot be created, so the preview cannot be created either
        return QImage();
    }

    QImage alphaMask = std::move(*mask);

    if (alphaMask.isNull() || !isBlack)
    {
        // The image is fully opaque, so the whole area is filled. The white fill looks
        // the same in both cases - a transparent area is displayed as a blank white
        // paper, which is exactly the color of the fill.
        return createFillImage(image.size(), isBlack);
    }

    // In the mask, a set sample means an opaque pixel, which the format Format_Mono
    // displays as white. The filled image is the other way round - the opaque part
    // becomes black and the transparent part stays a blank white paper.
    alphaMask.invertPixels();
    return alphaMask;
}

void PDFBitonalDocumentCreator::renderPages(const std::vector<PDFInteger>& pageIndices,
                                            const std::function<QSize(const PDFPage*)>& pageSizeGetter,
                                            const std::function<void(PDFInteger, QImage)>& pageImageProcessor,
                                            const PDFOperationControl* operationControl) const
{
    // The image of a page with rendering errors is the image of the page (owner 14. 11. 2026)
    const std::function<void(PDFInteger, QImage, const QList<PDFRenderError>&)> processor = [&pageImageProcessor](PDFInteger pageIndex, QImage image, const QList<PDFRenderError>&)
    {
        pageImageProcessor(pageIndex, std::move(image));
    };
    renderPageImages(pageIndices, pageSizeGetter, processor, operationControl);
}

void PDFBitonalDocumentCreator::renderPageImages(const std::vector<PDFInteger>& pageIndices,
                                                 const std::function<QSize(const PDFPage*)>& pageSizeGetter,
                                                 const std::function<void(PDFInteger, QImage, const QList<PDFRenderError>&)>& pageImageProcessor,
                                                 const PDFOperationControl* operationControl) const
{
    if (pageIndices.empty())
    {
        return;
    }

    Q_ASSERT(m_rasterizerPool);

    const PDFCatalog* catalog = m_document->getCatalog();

    // Rasterizer always renders the page as it is displayed, i.e. with the page
    // rotation applied. We want the image in the coordinate system of the page,
    // so a rotated page is rendered into a transposed image, which is then rotated
    // back. Rotating by a multiple of 90 degrees is a lossless operation.
    auto imageSizeGetter = [&pageSizeGetter](const PDFPage* page) -> QSize
    {
        const QSize size = pageSizeGetter(page);
        const PageRotation rotation = page->getPageRotation();

        if (rotation == PageRotation::Rotate90 || rotation == PageRotation::Rotate270)
        {
            return size.transposed();
        }

        return size;
    };

    auto processImage = [catalog, &pageImageProcessor](PDFRenderedPageImage& renderedPageImage)
    {
        // Owner 14. 11. 2026: a rendering error never blocks the conversion - the renderer has
        // skipped what it cannot process (a damaged image, a font, which cannot be loaded) and
        // rendered the rest; the errors go with the image, the caller tells the user to check it.
        // A null image is only an image, which could not be created.
        QImage image = compositePageImageOntoWhite(renderedPageImage.pageImage);

        const PDFPage* page = catalog->getPage(size_t(renderedPageImage.pageIndex));

        if (!image.isNull() && page)
        {
            QTransform transform;

            switch (page->getPageRotation())
            {
                case PageRotation::Rotate90:
                    transform.rotate(-90);
                    break;

                case PageRotation::Rotate180:
                    transform.rotate(180);
                    break;

                case PageRotation::Rotate270:
                    transform.rotate(90);
                    break;

                default:
                    break;
            }

            if (!transform.isIdentity())
            {
                image = image.transformed(transform);
            }
        }

        pageImageProcessor(renderedPageImage.pageIndex, std::move(image), renderedPageImage.getReportedErrors());
    };

    m_rasterizerPool->render(pageIndices, imageSizeGetter, processImage, nullptr, operationControl);
}

QImage PDFBitonalDocumentCreator::renderPage(PDFInteger pageIndex, QSize size, const PDFOperationControl* operationControl) const
{
    QImage result;

    auto pageSizeGetter = [size](const PDFPage*) { return size; };
    auto pageImageProcessor = [&result](PDFInteger, QImage image) { result = std::move(image); };

    renderPages({ pageIndex }, pageSizeGetter, pageImageProcessor, operationControl);

    return result;
}

QSizeF PDFBitonalDocumentCreator::getPageSizeInInches(const PDFPage* page)
{
    // The image covers the media box (the content is placed onto it); /UserUnit is the size of a
    // unit of the default user space in 1/72 inch (ISO 32000-2, 14.11.2, Table 31): a page of
    // 100 units with /UserUnit 10 is 1000/72 inch long, its image at 100 dpi has 1389 pixels and
    // the blank page detection measures its spots in millimeters of the paper
    return page->getMediaBox().normalized().size() * (PDF_POINT_TO_INCH * page->getEffectiveUserUnit());
}

QSize PDFBitonalDocumentCreator::getPageImageSize(const PDFPage* page, int dpiResolution)
{
    if (!page)
    {
        return QSize();
    }

    // The resolution comes from the caller, so it is clamped here - a page rasterized
    // at an extreme resolution would need gigabytes of memory for a single image.
    const int resolution = qBound(MINIMUM_DPI_RESOLUTION, dpiResolution, MAXIMUM_DPI_RESOLUTION);

    const QSizeF size = getPageSizeInInches(page) * resolution;
    const double maximumDimension = std::numeric_limits<int>::max() - 1.0;
    if (!std::isfinite(size.width()) || !std::isfinite(size.height()) ||
        size.width() <= 0.0 || size.height() <= 0.0 ||
        size.width() > maximumDimension || size.height() > maximumDimension)
    {
        // MediaBox is untrusted. Reject dimensions before rounding to int;
        // overflow must not turn an oversized page into a tiny blank image.
        return QSize();
    }

    // B65: a page larger than the limits of the pixels is refused (a 5 x 5 m page at 600 dpi
    // would need 22 GB), never rendered into a smaller image
    const QSize imageSize = size.toSize().expandedTo(QSize(1, 1));
    if (imageSize.width() > MAXIMUM_PAGE_IMAGE_DIMENSION || imageSize.height() > MAXIMUM_PAGE_IMAGE_DIMENSION ||
        PDFRasterizerPool::getImageDataSize(imageSize) > MAXIMUM_PAGE_IMAGE_BYTES)
    {
        return QSize();
    }
    return imageSize;
}

int PDFBitonalDocumentCreator::getPreviewDpiResolution(const PDFPage* page, int dpiResolution, QSize paneSize)
{
    const int resolution = qBound(MINIMUM_DPI_RESOLUTION, dpiResolution, MAXIMUM_DPI_RESOLUTION);
    const QSize outputSize = getPageImageSize(page, resolution);

    if (outputSize.isEmpty() || paneSize.isEmpty())
    {
        // No size to derive the resolution from
        return resolution;
    }

    if (outputSize.width() <= paneSize.width() && outputSize.height() <= paneSize.height())
    {
        // The page at the output resolution is small enough to be displayed as it is,
        // so the preview shows exactly, what the conversion is going to produce
        return resolution;
    }

    const double scale = qMin(double(paneSize.width()) / double(outputSize.width()),
                              double(paneSize.height()) / double(outputSize.height()));

    return qBound(MINIMUM_DPI_RESOLUTION, qRound(resolution * scale), resolution);
}

PDFBitonalDocumentCreator::Preview PDFBitonalDocumentCreator::createPreview(const PreviewRequest& request, const PDFOperationControl* operationControl) const
{
    Preview preview;
    QImage image = request.originalImage;

    if (image.isNull())
    {
        switch (request.conversionSource)
        {
            case ConversionSource::Images:
                image = getDecodedImage(request.item.imageReference, operationControl);
                preview.failure = isImageTooLarge(request.item.imageReference) ? FailureReason::TooLarge : FailureReason::CannotBeRead;
                break;

            case ConversionSource::Pages:
            {
                const PDFCatalog* catalog = m_document->getCatalog();
                const PDFPage* page = request.item.pageIndex >= 0 && size_t(request.item.pageIndex) < catalog->getPageCount() ? catalog->getPage(size_t(request.item.pageIndex)) : nullptr;
                const QSize size = getPageImageSize(page, request.dpiResolution);

                if (!page || page->getMediaBox().isEmpty())
                {
                    preview.failure = FailureReason::InvalidPage;
                }
                else if (!size.isValid())
                {
                    preview.failure = FailureReason::TooLarge;
                }
                else if (m_rasterizerPool)
                {
                    image = renderPage(request.item.pageIndex, size, operationControl);
                    preview.failure = FailureReason::RenderingErrors;
                }
                break;
            }
        }
    }

    if (image.isNull() || PDFOperationControl::isOperationCanceled(operationControl))
    {
        return preview;
    }

    preview.failure = std::nullopt;
    preview.originalImage = image;

    QImage alphaMask;
    int threshold = -1;
    preview.resultImage = createItemImage(image, request.conversionSource, request.item.mode, request.conversionMethod, request.manualThreshold, &alphaMask, &threshold, operationControl);
    preview.threshold = isConversionMode(request.item.mode) ? threshold : -1;

    if (request.item.isFilled() && request.conversionSource == ConversionSource::Images)
    {
        // The pane shows how the filled image looks in the document: its transparent part
        // stays a blank paper (the fill itself keeps the mask)
        const QImage fill = preview.resultImage;
        preview.resultImage = createFillPreviewImage(image, request.item.isFilledByBlack());
        if (fill.isNull())
        {
            preview.resultImage = QImage();
        }
    }
    else if (request.item.isFilled())
    {
        // A filled page is the fill over the whole page
        preview.resultImage = createFillImage(image.size(), request.item.isFilledByBlack());
    }

    if (PDFOperationControl::isOperationCanceled(operationControl))
    {
        return Preview();
    }

    if (preview.resultImage.isNull())
    {
        preview.failure = FailureReason::CannotBeRead;
        return preview;
    }

    if (request.isEncoded && request.item.isContentReplaced())
    {
        // Encoded as the conversion encodes it: a filled page is one sample
        const QImage encodedImage = request.item.isFilled() ? createItemImage(image, request.conversionSource, request.item.mode, request.conversionMethod, request.manualThreshold, nullptr, nullptr, nullptr)
                                                            : preview.resultImage;
        qint64 maskSize = 0;
        const PDFObject imageObject = request.conversionSource == ConversionSource::Images ? createConvertedImageObject(request.item.imageReference, encodedImage, alphaMask, request.compression, nullptr, &maskSize)
                                                                                           : createBitonalImageObject(encodedImage, request.compression);
        if (imageObject.isNull())
        {
            preview.failure = FailureReason::CannotBeEncoded;
            return preview;
        }

        preview.encodedSize = imageObject.getStream()->getContent()->size() + maskSize;
        preview.compression = getImageObjectCompression(imageObject);
    }

    return preview;
}

std::vector<PDFBitonalDocumentCreator::BlankPageResult> PDFBitonalDocumentCreator::detectBlankPages(const std::vector<PDFInteger>& pageIndices,
                                                                                                   const PDFOperationControl* operationControl,
                                                                                                   PDFProgressSink* progress) const
{
    std::vector<BlankPageResult> results(pageIndices.size());
    std::map<PDFInteger, size_t> resultIndices;
    std::vector<PDFInteger> renderedPages;
    const size_t pageCount = m_document->getCatalog()->getPageCount();

    for (size_t index = 0; index < pageIndices.size(); ++index)
    {
        results[index].pageIndex = pageIndices[index];

        // A page given twice is examined once, a page, which does not exist, never
        if (pageIndices[index] >= 0 && size_t(pageIndices[index]) < pageCount && resultIndices.emplace(pageIndices[index], index).second)
        {
            renderedPages.push_back(pageIndices[index]);
        }
    }

    if (progress)
    {
        ProgressStartupInfo info;
        info.text = PDFTranslationContext::tr("Detecting blank pages");
        progress->start(qMax<size_t>(renderedPages.size(), 1), std::move(info));
    }

    if (m_rasterizerPool && !renderedPages.empty())
    {
        // The detection has a resolution of its own - not the resolution of the conversion:
        // the analysis measures the spots of the ink in millimeters and needs no more detail
        constexpr int dpiResolution = BLANK_PAGE_DPI_RESOLUTION;
        QMutex progressMutex;

        auto pageSizeGetter = [](const PDFPage* page) { return getPageImageSize(page, dpiResolution); };
        auto pageImageProcessor = [&results, &resultIndices, &progressMutex, progress, operationControl](PDFInteger pageIndex, QImage image, const QList<PDFRenderError>& errors)
        {
            // A page, which could not be rendered, arrives as a null image: it is examined
            // and it is not blank. A page, whose rendering skipped a part of its content (an
            // error: a damaged image, a font, which cannot be loaded), is never blank either -
            // the missing part may be its only content, and a white fill would wipe it out.
            // Each page has its own element of the results, the map is only read.
            const bool isIncomplete = std::any_of(errors.cbegin(), errors.cend(), [](const PDFRenderError& error) { return error.type == RenderErrorType::Error || error.type == RenderErrorType::NotImplemented; });
            const BlankPageInfo info = detectBlankPage(isIncomplete ? QImage() : image, dpiResolution, operationControl);
            BlankPageResult& result = results[resultIndices.at(pageIndex)];
            result.isExamined = !PDFOperationControl::isOperationCanceled(operationControl);
            result.isBlank = result.isExamined && info.isBlank;

            if (progress)
            {
                QMutexLocker lock(&progressMutex);
                progress->step();
            }
        };

        renderPageImages(renderedPages, pageSizeGetter, pageImageProcessor, operationControl);
    }

    if (progress)
    {
        progress->finish();
    }

    // A page given twice has the result of its first place
    for (BlankPageResult& result : results)
    {
        auto it = resultIndices.find(result.pageIndex);
        if (it != resultIndices.cend())
        {
            result = results[it->second];
        }
    }

    return results;
}

int PDFBitonalDocumentCreator::getEstimatedDpiResolution() const
{
    PDFDocumentDataLoaderDecorator loader(m_document);
    int dpiResolution = 0;

    // Scanned documents store the page as an image covering the whole page. We estimate
    // the resolution from the size of these images, so the rasterized page keeps the
    // details of the original scan. We do not know, which part of the page the image
    // actually covers (that would require an analysis of the content stream), so the
    // estimate is only used to raise the resolution above the default one - a small
    // logo must never lower it.
    const PDFCatalog* catalog = m_document->getCatalog();
    for (size_t pageIndex = 0, pageCount = catalog->getPageCount(); pageIndex < pageCount; ++pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);

        if (!page)
        {
            continue;
        }

        const QRectF mediaBox = page->getMediaBox();
        if (!std::isfinite(mediaBox.width()) || !std::isfinite(mediaBox.height()) ||
            mediaBox.width() < 1.0 || mediaBox.height() < 1.0)
        {
            continue;
        }

        const QSizeF pageInches = getPageSizeInInches(page);
        traversePageImages(page, [&](PDFObjectReference, const PDFDictionary* imageDictionary)
        {
            const PDFInteger width = loader.readIntegerFromDictionary(imageDictionary, "Width", 0);
            const PDFInteger height = loader.readIntegerFromDictionary(imageDictionary, "Height", 0);

            // An image inside a form XObject is scaled by the matrix of the form, which
            // is not analyzed here, so the estimate is even rougher for it. It can only
            // raise the resolution, so an inaccurate estimate cannot lose any detail.
            if (width > 0)
            {
                dpiResolution = qMax(dpiResolution, qRound(qBound(0.0, width / pageInches.width(), double(MAXIMUM_DPI_RESOLUTION))));
            }

            if (height > 0)
            {
                dpiResolution = qMax(dpiResolution, qRound(qBound(0.0, height / pageInches.height(), double(MAXIMUM_DPI_RESOLUTION))));
            }
        });
    }

    return qBound(DEFAULT_DPI_RESOLUTION, dpiResolution, MAXIMUM_DPI_RESOLUTION);
}

bool PDFBitonalDocumentCreator::isStencilMask(PDFObjectReference reference) const
{
    if (const PDFDictionary* dictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference)))
    {
        PDFDocumentDataLoaderDecorator loader(m_document);
        return loader.readBooleanFromDictionary(dictionary, "ImageMask", false);
    }

    return false;
}

std::optional<PDFImage> PDFBitonalDocumentCreator::getImageFromReference(PDFObjectReference reference) const
{
    std::optional<PDFImage> pdfImage;
    PDFObject imageObject = m_document->getObjectByReference(reference);
    PDFRenderErrorReporterDummy errorReporter;

    if (!imageObject.isStream())
    {
        // Image is not stream
        return pdfImage;
    }

    const PDFStream* stream = imageObject.getStream();
    try
    {
        PDFColorSpacePointer colorSpace;
        const PDFDictionary* streamDictionary = stream->getDictionary();
        if (streamDictionary->hasKey("ColorSpace"))
        {
            const PDFObject& colorSpaceObject = m_document->getObject(streamDictionary->get("ColorSpace"));
            if (colorSpaceObject.isName() || colorSpaceObject.isArray())
            {
                PDFDictionaryBuilder dummyDictionary;
                colorSpace = PDFAbstractColorSpace::createColorSpace(dummyDictionary.getDictionary(), m_document, &m_iccProfileCache, colorSpaceObject);
            }
        }
        pdfImage.emplace(PDFImage::createImage(m_document,
                                               stream,
                                               colorSpace,
                                               false,
                                               RenderingIntent::Perceptual,
                                               &errorReporter));
    }
    catch (const PDFException&)
    {
        // Do nothing
    }
    catch (const PDFRendererException&)
    {
        // A damaged image (an explicit /Mask, which is no stencil mask, 8.9.6.3) is not decoded
    }

    return pdfImage;
}

bool PDFBitonalDocumentCreator::isImageTooLarge(PDFObjectReference reference) const
{
    const PDFDictionary* dictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference));
    if (!dictionary)
    {
        return false;
    }

    PDFDocumentDataLoaderDecorator loader(m_document);
    const PDFInteger width = loader.readIntegerFromDictionary(dictionary, "Width", 0);
    const PDFInteger height = loader.readIntegerFromDictionary(dictionary, "Height", 0);
    if (width <= 0 || height <= 0)
    {
        // Nothing to allocate: the decoding refuses the image itself
        return false;
    }

    // A side over 2^31 is too large by itself; below it the product fits into 64 bits
    constexpr PDFInteger maximumSide = std::numeric_limits<int>::max();
    if (width > maximumSide || height > maximumSide)
    {
        return true;
    }
    return qint64(width) * qint64(height) > MAXIMUM_DECODED_IMAGE_BYTES / 4;
}

QImage PDFBitonalDocumentCreator::getDecodedImage(PDFObjectReference reference, const PDFOperationControl* operationControl) const
{
    // Plan 6 of step 4: a declaration of billions of pixels is never allocated
    if (isImageTooLarge(reference))
    {
        return QImage();
    }

    std::optional<PDFImage> pdfImage = getImageFromReference(reference);

    if (!pdfImage)
    {
        return QImage();
    }

    PDFCMSGeneric genericCms;
    PDFRenderErrorReporterDummy errorReporter;

    try
    {
        return pdfImage->getImage(&genericCms, &errorReporter, operationControl);
    }
    catch (const PDFException&)
    {
        return QImage();
    }
}

void PDFBitonalDocumentCreator::startProgress(size_t stepCount, QString text)
{
    if (!m_progress)
    {
        return;
    }

    ProgressStartupInfo info;
    info.showDialog = true;
    info.text = std::move(text);
    m_progress->start(stepCount, std::move(info));
}

void PDFBitonalDocumentCreator::stepProgress()
{
    if (m_progress)
    {
        // The pages are converted by more threads at once; a progress is called by one
        // thread at a time
        QMutexLocker lock(&m_progressMutex);
        m_progress->step();
    }
}

void PDFBitonalDocumentCreator::finishProgress()
{
    if (m_progress)
    {
        m_progress->finish();
    }
}

}   // namespace pdf
