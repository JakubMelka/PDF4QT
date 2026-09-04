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

#include "pdfbitonaldocumentcreator.h"
#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfobjectutils.h"
#include "pdfoptimizer.h"
#include "pdfpage.h"
#include "pdfprogress.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"

#include <QPainter>
#include <QScopeGuard>

#include <map>

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
    result.fill(Qt::white);

    QPainter painter(&result);
    painter.drawImage(0, 0, image);
    painter.end();

    return result;
}

PDFBitonalDocumentCreator::PDFBitonalDocumentCreator(const PDFDocument* document,
                                                     PDFRasterizerPool* rasterizerPool,
                                                     PDFProgress* progress) :
    m_document(document),
    m_rasterizerPool(rasterizerPool),
    m_progress(progress)
{
    Q_ASSERT(m_document);
}

PDFRenderer::Features PDFBitonalDocumentCreator::getPageRasterizationFeatures()
{
    return PDFRenderer::Features(PDFRenderer::Antialiasing |
                                 PDFRenderer::TextAntialiasing |
                                 PDFRenderer::SmoothImages |
                                 PDFRenderer::ClipToCropBox);
}

bool PDFBitonalDocumentCreator::createBitonalDocument(const Settings& settings)
{
    m_bitonalDocument = PDFDocument();

    try
    {
        PDFDocumentBuilder builder(m_document);
        bool isConverted = false;

        switch (settings.conversionSource)
        {
            case ConversionSource::Images:
                isConverted = createBitonalDocumentFromImages(builder, settings);
                break;

            case ConversionSource::Pages:
                isConverted = createBitonalDocumentFromPages(builder, settings);
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        if (!isConverted)
        {
            // Nothing has been converted
            return false;
        }

        PDFDocument builtDocument = builder.build();

        // Images and content streams, which have been replaced, are not referenced
        // by the document anymore. They must be removed, otherwise the converted
        // document would be even larger than the original one.
        PDFOptimizer optimizer(PDFOptimizer::RemoveUnusedObjects, nullptr);
        optimizer.setDocument(&builtDocument);
        optimizer.optimize();

        m_bitonalDocument = PDFDocument(optimizer.takeStorage(), builtDocument.getInfo()->version, QByteArray());
        return true;
    }
    catch (const PDFException&)
    {
        m_bitonalDocument = PDFDocument();
        return false;
    }
}

bool PDFBitonalDocumentCreator::createBitonalDocumentFromImages(PDFDocumentBuilder& builder, const Settings& settings)
{
    std::vector<ItemInfo> itemsToBeConverted;
    std::copy_if(settings.items.begin(), settings.items.end(), std::back_inserter(itemsToBeConverted), [](const auto& item) { return item.isContentReplaced(); });

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
        const PDFObjectReference reference = item.imageReference;
        QImage image = getDecodedImage(reference, nullptr);

        if (image.isNull())
        {
            stepProgress();
            continue;
        }

        QImage alphaMask;
        QImage bitonalImage;

        if (item.isFilled())
        {
            // Only the color samples are replaced by the solid fill - the transparency
            // of the original image is preserved. Filling a masked text layer of a
            // two-layer scan without its mask would cover the whole page.
            alphaMask = PDFImageConversion::createAlphaMask(image);
            bitonalImage = createFillImage(alphaMask.isNull() ? QSize(1, 1) : alphaMask.size(), item.isFilledByBlack());
        }
        else
        {
            bitonalImage = convertImageToBitonal(image, settings.conversionMethod, settings.manualThreshold, &alphaMask);
        }

        PDFObject imageObject = createBitonalImageObject(bitonalImage);

        if (!imageObject.isNull())
        {
            PDFDictionary dictionary = *imageObject.getStream()->getDictionary();
            QByteArray content = *imageObject.getStream()->getContent();

            // Transfer the entries of the original image, which are not related
            // to the image samples, into the converted image.
            if (const PDFDictionary* originalDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference)))
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
                PDFObject softMaskObject = createBitonalImageObject(alphaMask);

                if (!softMaskObject.isNull())
                {
                    PDFObjectReference softMaskReference = builder.addObject(std::move(softMaskObject));
                    dictionary.setEntry(PDFInplaceOrMemoryString("SMask"), PDFObject::createReference(softMaskReference));
                }
            }

            builder.setObject(reference, PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), std::move(content))));
            isConverted = true;
        }

        stepProgress();
    }

    return isConverted;
}

bool PDFBitonalDocumentCreator::createBitonalDocumentFromPages(PDFDocumentBuilder& builder, const Settings& settings)
{
    // Pages, whose content is replaced, and the mode of each of them. Only the pages
    // converted by the algorithm have to be rasterized - a filled page is built from
    // a single sample, so it costs nothing.
    std::vector<PDFInteger> pageIndices;
    std::vector<PDFInteger> rasterizedPageIndices;
    std::map<PDFInteger, ItemMode> pageModes;

    for (const ItemInfo& item : settings.items)
    {
        if (item.isContentReplaced() && item.pageIndex >= 0)
        {
            pageIndices.push_back(item.pageIndex);
            pageModes[item.pageIndex] = item.mode;

            if (item.mode == ItemMode::Algorithm)
            {
                rasterizedPageIndices.push_back(item.pageIndex);
            }
        }
    }

    // Do we have something to be converted?
    if (pageIndices.empty())
    {
        return false;
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
    const PDFCatalog* catalog = m_document->getCatalog();
    const int dpiResolution = settings.dpiResolution;

    // Pages are rasterized and converted in parallel, but the converted images are
    // stored as encoded image streams only - the rasterized images are large and we
    // do not want to keep all of them in the memory at once.
    std::vector<PDFObject> imageObjects(catalog->getPageCount());

    auto pageSizeGetter = [dpiResolution](const PDFPage* page) { return getPageImageSize(page, dpiResolution); };
    auto pageImageProcessor = [this, &imageObjects, &settings](PDFInteger pageIndex, QImage image)
    {
        QImage bitonalImage = convertImageToBitonal(image, settings.conversionMethod, settings.manualThreshold, nullptr);
        imageObjects[size_t(pageIndex)] = createBitonalImageObject(bitonalImage);
        stepProgress();
    };

    renderPages(rasterizedPageIndices, pageSizeGetter, pageImageProcessor, nullptr);

    const bool isWholeDocumentConverted = pageIndices.size() == catalog->getPageCount();

    for (const PDFInteger pageIndex : pageIndices)
    {
        const ItemMode mode = pageModes.at(pageIndex);
        PDFObject imageObject;

        if (mode == ItemMode::Algorithm)
        {
            imageObject = std::move(imageObjects[size_t(pageIndex)]);
        }
        else
        {
            // Page is covered by a solid area, which does not need any rasterization
            imageObject = createBitonalImageObject(createFillImage(QSize(1, 1), mode == ItemMode::FillBlack));
        }

        const PDFPage* page = catalog->getPage(size_t(pageIndex));

        if (imageObject.isNull() || !page)
        {
            continue;
        }

        const QRectF mediaBox = page->getMediaBox();
        if (mediaBox.isEmpty())
        {
            continue;
        }

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

        PDFDictionary contentStreamDictionary;
        contentStreamDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        contentStreamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedContentStream.size()));

        const PDFObjectReference contentStreamReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(contentStreamDictionary), std::move(compressedContentStream))));

        PDFDictionary xobjectDictionary;
        xobjectDictionary.setEntry(PDFInplaceOrMemoryString("BitonalImage"), PDFObject::createReference(imageReference));

        PDFArray procSetArray;
        procSetArray.appendItem(PDFObject::createName("PDF"));
        procSetArray.appendItem(PDFObject::createName("ImageB"));

        PDFDictionary resourcesDictionary;
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(xobjectDictionary))));
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("ProcSet"), PDFObject::createArray(std::make_shared<PDFArray>(std::move(procSetArray))));

        const PDFObjectReference pageReference = page->getPageReference();
        const PDFDictionary* originalPageDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(pageReference));

        if (!originalPageDictionary)
        {
            continue;
        }

        // Everything except the page content is preserved - the page keeps its size,
        // rotation, annotations and other properties.
        PDFDictionary pageDictionary = *originalPageDictionary;
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contentStreamReference));
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(resourcesDictionary))));

        // The marked content of the page is gone, so the page must not be a part
        // of the structure tree anymore.
        pageDictionary.removeEntry("StructParents");

        // The embedded thumbnail shows the original, colored content of the page.
        // Keeping it would both display a wrong preview in the viewer and prevent
        // the removal of the unused objects it references.
        pageDictionary.removeEntry("Thumb");

        builder.setObject(pageReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(pageDictionary))));
        isConverted = true;
    }

    if (isConverted && isWholeDocumentConverted)
    {
        // No page is tagged anymore, so the whole structure tree can be removed
        const PDFObjectReference catalogReference = builder.getCatalogReference();

        if (const PDFDictionary* originalCatalogDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(catalogReference)))
        {
            PDFDictionary catalogDictionary = *originalCatalogDictionary;
            catalogDictionary.removeEntry("StructTreeRoot");
            catalogDictionary.removeEntry("MarkInfo");
            builder.setObject(catalogReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(catalogDictionary))));
        }
    }

    return isConverted;
}

std::vector<PDFObjectReference> PDFBitonalDocumentCreator::getConvertibleImages() const
{
    PDFObjectClassifier classifier;
    classifier.classify(m_document);

    std::vector<PDFObjectReference> references = classifier.getObjectsByType(PDFObjectClassifier::Image);

    std::erase_if(references, [this](PDFObjectReference reference) { return isStencilMask(reference); });

    return references;
}

QImage PDFBitonalDocumentCreator::convertImageToBitonal(const QImage& image,
                                                        PDFImageConversion::ConversionMethod conversionMethod,
                                                        int threshold,
                                                        QImage* alphaMask)
{
    if (image.isNull())
    {
        return QImage();
    }

    PDFImageConversion imageConversion;
    imageConversion.setConversionMethod(conversionMethod);
    imageConversion.setThreshold(threshold);
    imageConversion.setAlphaMode(PDFImageConversion::AlphaMode::Composite);
    imageConversion.setImage(image);

    if (!imageConversion.convert())
    {
        return QImage();
    }

    if (alphaMask)
    {
        *alphaMask = imageConversion.getConvertedAlphaMask();
    }

    return imageConversion.getConvertedImage();
}

PDFObject PDFBitonalDocumentCreator::createBitonalImageObject(const QImage& image)
{
    if (image.isNull())
    {
        return PDFObject();
    }

    try
    {
        PDFImage::ImageEncodeOptions options;
        options.compression = PDFImage::ImageCompression::Flate;
        options.colorMode = PDFImage::ImageColorMode::Monochrome;
        options.enablePngPredictor = true;

        PDFStream stream = PDFImage::createStreamFromImage(image, options, nullptr);

        PDFDictionary dictionary = *stream.getDictionary();
        QByteArray content = *stream.getContent();

        return PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), std::move(content)));
    }
    catch (const PDFException&)
    {
        return PDFObject();
    }
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

    QImage alphaMask = PDFImageConversion::createAlphaMask(image);

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

        pageImageProcessor(renderedPageImage.pageIndex, std::move(image));
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

QSize PDFBitonalDocumentCreator::getPageImageSize(const PDFPage* page, int dpiResolution)
{
    Q_ASSERT(page);

    const QSizeF size = page->getMediaBox().size() * PDF_POINT_TO_INCH * dpiResolution;
    return size.toSize().expandedTo(QSize(1, 1));
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
        if (mediaBox.width() < 1.0 || mediaBox.height() < 1.0)
        {
            continue;
        }

        // Resources are an inheritable attribute of the page, so they can be stored
        // in a parent node of the page tree. PDFPage resolves the inheritance for us.
        const PDFDictionary* resourcesDictionary = m_document->getDictionaryFromObject(page->getResources());
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
            const PDFDictionary* imageDictionary = m_document->getDictionaryFromObject(xobjectDictionary->getValue(index));

            if (!imageDictionary || loader.readNameFromDictionary(imageDictionary, "Subtype") != "Image")
            {
                continue;
            }

            const PDFInteger width = loader.readIntegerFromDictionary(imageDictionary, "Width", 0);
            const PDFInteger height = loader.readIntegerFromDictionary(imageDictionary, "Height", 0);

            if (width > 0)
            {
                dpiResolution = qMax(dpiResolution, qRound(width / (mediaBox.width() * PDF_POINT_TO_INCH)));
            }

            if (height > 0)
            {
                dpiResolution = qMax(dpiResolution, qRound(height / (mediaBox.height() * PDF_POINT_TO_INCH)));
            }
        }
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
                PDFDictionary dummyDictionary;
                colorSpace = PDFAbstractColorSpace::createColorSpace(&dummyDictionary, m_document, colorSpaceObject);
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

    return pdfImage;
}

QImage PDFBitonalDocumentCreator::getDecodedImage(PDFObjectReference reference, const PDFOperationControl* operationControl) const
{
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
