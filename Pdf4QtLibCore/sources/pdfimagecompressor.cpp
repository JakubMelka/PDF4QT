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

#include "pdfimagecompressor.h"

#include "pdfcatalog.h"
#include "pdfcolorspaces.h"
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfimage.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfoperationcontrol.h"
#include "pdfpage.h"
#include "pdfpagecontentprocessor.h"
#include "pdfprogress.h"
#include "pdffont.h"
#include "pdfutils.h"

#include <QMutexLocker>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace pdf
{

/// Helpers of the collection of the images
class PDFImageCompressorHelper
{
public:
    PDFImageCompressorHelper() = delete;

    /// Length of an axis of the unit square of an image in inches; 0 for a
    /// degenerated axis and for a length, which is not finite
    static double getAxisInches(qreal x, qreal y)
    {
        const double length = std::hypot(static_cast<double>(x), static_cast<double>(y)) * PDF_POINT_TO_INCH;
        return std::isfinite(length) && length > std::numeric_limits<double>::epsilon() ? length : 0.0;
    }

    /// Resolution of pixels on a length in inches, 0 for a degenerated axis
    static double getDpi(unsigned int pixels, double inches)
    {
        return inches > 0.0 ? static_cast<double>(pixels) / inches : 0.0;
    }

    /// Tasks running at once: one thread of the pool stays free for the canvas, one thread of a
    /// strategy, which does not run the pages in threads
    static int getTaskCount()
    {
        const int maximumThreads = PDFExecutionPolicy::getMaxThreadCount(PDFExecutionPolicy::Scope::Page);
        return PDFExecutionPolicy::isParallelizing(PDFExecutionPolicy::Scope::Page) ? qMax(1, maximumThreads - 1) : 1;
    }

    /// Calls the function for the indices 0 .. count - 1 in the threads of the policy. A fixed number
    /// of tasks takes the indices one by one, so the queue of the pool is never full of them: one thread
    /// of the pool stays free, a page of the canvas waits for no more than one of its tasks.
    template<typename Function>
    static void executeForEachIndex(size_t count, const PDFOperationControl* operationControl, const Function& function)
    {
        if (count == 0)
        {
            return;
        }

        const int taskCount = int(qMin(size_t(getTaskCount()), count));

        std::atomic<size_t> nextIndex = 0;
        PDFIntegerRange<int> tasks(0, taskCount);
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, tasks.begin(), tasks.end(), [&](int)
        {
            while (!PDFOperationControl::isOperationCanceled(operationControl))
            {
                const size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                if (index >= count)
                {
                    break;
                }

                function(index);
            }
        });
    }

    static PDFImageCompressor::TestDecodeHook& getTestDecodeHook()
    {
        static PDFImageCompressor::TestDecodeHook hook;
        return hook;
    }

    /// The image of an image XObject drawn on a page (the walk of all layers, step 4 of phase 2,
    /// R4-4: the changing tools and Compress see the images of every layer). Created from its own
    /// dictionary, not from the resources of the page: an image XObject declares its color space by
    /// a family name or an array (ISO 32000-2, 8.9.5, Table 87), and a re-encoded image is written
    /// as a device space, which a /DefaultRGB of the page would show again - decoding it without the
    /// default space gives the colors, which the written image shows the same way. The intent is the
    /// default intent of a graphics state (Perceptual), /Intent of the image overrides it.
    /// \returns std::nullopt for an inline image, a stream without a reference and an image, which
    ///          cannot be created (an error of its dictionary or of its color space)
    static std::optional<PDFImage> createImage(const PDFDocument* document, const PDFImageCompressor::DrawnImage& drawnImage)
    {
        if (!drawnImage.reference.isValid() || !drawnImage.stream)
        {
            return std::nullopt;
        }

        try
        {
            PDFRenderErrorReporterDummy reporter;
            PDFColorSpacePointer colorSpace;
            if (drawnImage.colorSpace.isName() || drawnImage.colorSpace.isArray())
            {
                colorSpace = PDFAbstractColorSpace::createColorSpace(nullptr, document, nullptr, drawnImage.colorSpace);
            }
            else if (!drawnImage.colorSpace.isNull())
            {
                return std::nullopt;
            }
            return PDFImage::createImage(document, drawnImage.stream, std::move(colorSpace), false, RenderingIntent::Perceptual, &reporter);
        }
        catch (const PDFException&)
        {
            return std::nullopt;
        }
        catch (const PDFRendererException&)
        {
            // A damaged image (an explicit /Mask, which is no stencil mask, 8.9.6.3) is not decoded
            return std::nullopt;
        }
    }
};


/// Content processor, which only reports the image XObjects drawn on the page.
/// With a placement callback it reports them without decoding them; with a
/// filter it decodes only the images of the filter.
class PDFImageCompressor::ImageCollectorProcessor : public PDFPageContentProcessor
{
public:
    ImageCollectorProcessor(const PDFPage* page,
                            const PDFDocument* document,
                            const PDFFontCache* fontCache,
                            const PDFICCProfileCache* iccProfileCache,
                            const PDFCMS* cms,
                            const PDFOptionalContentActivity* optionalContentActivity,
                            const PDFMeshQualitySettings& meshQualitySettings,
                            const ImageCallback* callback,
                            const PlacementCallback* placementCallback,
                            const ImageFilter* filter,
                            const DrawnImageCallback* drawnImageCallback = nullptr) :
        PDFPageContentProcessor(page, document, fontCache, iccProfileCache, cms, optionalContentActivity, QTransform(), meshQualitySettings),
        m_callback(callback),
        m_placementCallback(placementCallback),
        m_filter(filter),
        m_drawnImageCallback(drawnImageCallback)
    {
    }

protected:
    virtual bool isContentKindSuppressed(ContentKind kind) const override
    {
        switch (kind)
        {
            case ContentKind::Images:
            case ContentKind::Tiling:
            case ContentKind::Forms:
                return false;

            default:
                return true;
        }
    }

    virtual bool isTextIgnored() const override
    {
        return true;
    }

    virtual bool isOriginalImageRequired(const PDFStream* stream, PDFObjectReference reference) override
    {
        Q_UNUSED(stream);

        if (isContentSuppressed() || isProcessingCanceled())
        {
            return false;
        }

        if (m_drawnImageCallback)
        {
            // The collection of step 4: every drawn image, inline images too, nothing is decoded
            reportDrawnImage(stream, reference);
            return false;
        }

        if (!reference.isValid())
        {
            // Inline images or direct streams without reference are skipped, as they can't be shared
            return false;
        }

        if (m_placementCallback)
        {
            (*m_placementCallback)(reference, calculatePlacedSize());
            return false;
        }

        return !m_filter || !(*m_filter) || (*m_filter)(reference);
    }

    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override
    {
        if (isContentSuppressed() || isProcessingCanceled())
        {
            return true;
        }

        if (!reference.isValid() || !stream)
        {
            // Inline images or direct streams without reference are skipped, as they can't be shared
            return true;
        }

        const QPointF inches = calculatePlacedSize();
        (*m_callback)(image, stream, reference, QPointF(PDFImageCompressorHelper::getDpi(image.getImageData().getWidth(), inches.x()),
                                                        PDFImageCompressorHelper::getDpi(image.getImageData().getHeight(), inches.y())));
        return true;
    }

private:
    /// Length of the axes of the image on the page in inches
    QPointF calculatePlacedSize() const
    {
        const QTransform ctm = getGraphicState()->getCurrentTransformationMatrix();
        return QPointF(PDFImageCompressorHelper::getAxisInches(ctm.m11(), ctm.m12()),
                       PDFImageCompressorHelper::getAxisInches(ctm.m21(), ctm.m22()));
    }

    /// Reports an image of the collection of step 4 (DrawnImageCallback)
    void reportDrawnImage(const PDFStream* stream, PDFObjectReference reference)
    {
        if (!stream)
        {
            return;
        }

        DrawnImage image;
        image.reference = reference;
        image.stream = stream;
        image.inches = calculatePlacedSize();

        const PDFDictionary* dictionary = stream->getDictionary();
        image.colorSpace = getDocument()->getObject(dictionary->get("ColorSpace"));
        if (!reference.isValid())
        {
            const InlineImagePosition& position = getInlineImagePosition();
            if (position.index < 0)
            {
                // A direct stream, which is no inline image (streams are indirect objects, 7.3.8)
                return;
            }
            image.inlineContentStream = position.contentStream;
            image.inlineIndex = position.index;
        }

        // An inline image may name a color space of the resources (8.9.7, Table 91): its value is
        // reported; the abbreviations of the device spaces are not resources. An image XObject names
        // a family (Table 87), but files name a resource too - the content processor finds it in the
        // resources of the content, so the walk reports its value as well (the decoding of all layers
        // and the color of PDFDocumentImageList read it)
        if (image.colorSpace.isName())
        {
            const PDFDictionary* colorSpaces = getColorSpaceDictionary();
            const QByteArray name = image.colorSpace.getString();
            if (colorSpaces && colorSpaces->hasKey(name))
            {
                image.colorSpace = getDocument()->getObject(colorSpaces->get(name));
            }
        }

        (*m_drawnImageCallback)(image);
    }

    const ImageCallback* m_callback;
    const PlacementCallback* m_placementCallback;
    const ImageFilter* m_filter;
    const DrawnImageCallback* m_drawnImageCallback;
};

PDFImageCompressor::Environment::Environment(const PDFDocument* document, PDFCMSPointer cms) :
    m_document(document),
    m_cms(std::move(cms)),
    m_optionalContentActivity(std::make_unique<PDFOptionalContentActivity>(document, OCUsage::Export, nullptr)),
    m_fontCache(std::make_unique<PDFFontCache>(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT)),
    m_iccProfileCache(std::make_unique<PDFICCProfileCache>())
{
    if (!m_cms)
    {
        PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(document);
        cmsManager.setSettings(cmsManager.getDefaultSettings());
        m_cms = cmsManager.getCurrentCMS();
    }

    PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), m_optionalContentActivity.get());
    m_fontCache->setDocument(modifiedDocument);
    m_iccProfileCache->setDocument(modifiedDocument);
}

PDFImageCompressor::Environment::~Environment() = default;

void PDFImageCompressor::Environment::processPage(PDFInteger pageIndex, const ImageCallback& callback, const PDFOperationControl* operationControl, const ImageFilter& filter) const
{
    const PDFCatalog* catalog = m_document ? m_document->getCatalog() : nullptr;
    const PDFPage* page = catalog && pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(pageIndex) : nullptr;
    if (!page || PDFOperationControl::isOperationCanceled(operationControl))
    {
        return;
    }

    // The fonts of a page are held only for the page: each call has its own place in the guards,
    // more threads process more pages at once (the walk realizes no font, isTextIgnored)
    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCacheShrinkGuard shrinkGuard(m_fontCache.get(), &meshQualitySettings);
    ImageCollectorProcessor processor(page, m_document, m_fontCache.get(), m_iccProfileCache.get(), m_cms.data(), m_optionalContentActivity.get(), meshQualitySettings, &callback, nullptr, &filter);
    processor.setOperationControl(operationControl);
    processor.processContents();
}

void PDFImageCompressor::Environment::processPagePlacements(PDFInteger pageIndex, const PlacementCallback& callback, const PDFOperationControl* operationControl) const
{
    const PDFCatalog* catalog = m_document ? m_document->getCatalog() : nullptr;
    const PDFPage* page = catalog && pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(pageIndex) : nullptr;
    if (!page || PDFOperationControl::isOperationCanceled(operationControl))
    {
        return;
    }

    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCacheShrinkGuard shrinkGuard(m_fontCache.get(), &meshQualitySettings);
    ImageCollectorProcessor processor(page, m_document, m_fontCache.get(), m_iccProfileCache.get(), m_cms.data(), m_optionalContentActivity.get(), meshQualitySettings, nullptr, &callback, nullptr);
    processor.setOperationControl(operationControl);
    processor.processContents();
}

void PDFImageCompressor::Environment::processPageImages(PDFInteger pageIndex, const DrawnImageCallback& callback, const PDFOptionalContentActivity* optionalContentActivity, const PDFOperationControl* operationControl) const
{
    const PDFCatalog* catalog = m_document ? m_document->getCatalog() : nullptr;
    const PDFPage* page = catalog && pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(pageIndex) : nullptr;
    if (!page || !callback || PDFOperationControl::isOperationCanceled(operationControl))
    {
        return;
    }

    // Without an activity the processor suppresses no optional content (every layer, R4-4)
    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCacheShrinkGuard shrinkGuard(m_fontCache.get(), &meshQualitySettings);
    ImageCollectorProcessor processor(page, m_document, m_fontCache.get(), m_iccProfileCache.get(), m_cms.data(), optionalContentActivity, meshQualitySettings, nullptr, nullptr, nullptr, &callback);
    processor.setOperationControl(operationControl);
    processor.processContents();
}

double PDFImageCompressor::updateAxisDpi(double currentValue, double candidate)
{
    if (candidate <= 0.0 || !std::isfinite(candidate))
    {
        return currentValue;
    }

    if (!std::isfinite(currentValue) || candidate < currentValue)
    {
        return candidate;
    }

    return currentValue;
}

PDFImageCompressor::ImageStatisticsList PDFImageCompressor::collectImages(const PDFDocument* document, const PDFOperationControl* operationControl, PDFProgressSink* progress) const
{
    ImageStatisticsList result;
    if (!document || !document->getCatalog())
    {
        return result;
    }

    Environment environment(document);
    return collectImages(environment, operationControl, progress);
}

PDFImageCompressor::ImageStatisticsList PDFImageCompressor::collectImages(const Environment& environment, const PDFOperationControl* operationControl, PDFProgressSink* progress) const
{
    ImageStatisticsList result;
    const ImagePlacements placements = collectImagePlacements(environment, operationControl, progress);
    result.reserve(placements.size());

    const bool hasProgress = progress && !placements.empty() && !PDFOperationControl::isOperationCanceled(operationControl);
    if (hasProgress)
    {
        progress->start(placements.size(), ProgressStartupInfo());
    }

    const DecodedImageCallback addImage = [&result, progress, hasProgress](ImageStatistics statistics)
    {
        result.push_back(std::move(statistics));
        if (hasProgress)
        {
            progress->step();
        }
    };
    visitImages(environment, placements, addImage, operationControl);

    if (hasProgress)
    {
        progress->finish();
    }

    // The order of the references, not of the pages
    std::sort(result.begin(), result.end(), [](const ImageStatistics& left, const ImageStatistics& right) { return left.reference < right.reference; });
    return result;
}

PDFImageCompressor::ImagePlacements PDFImageCompressor::collectImagePlacements(const Environment& environment, const PDFOperationControl* operationControl, PDFProgressSink* progress) const
{
    ImagePlacements result;
    const PDFDocument* document = environment.getDocument();
    if (!document || !document->getCatalog())
    {
        return result;
    }

    const size_t pageCount = document->getCatalog()->getPageCount();
    if (progress && pageCount > 0)
    {
        progress->start(pageCount, ProgressStartupInfo());
    }

    // What each page draws; put together in the order of the pages, so the result does not
    // depend on the threads. A progress serves one thread at a time.
    std::vector<std::vector<std::tuple<PDFObjectReference, QPointF, PDFObject>>> pagePlacements(pageCount);
    QMutex progressMutex;
    const auto processPage = [&](size_t pageIndex)
    {
        std::vector<std::tuple<PDFObjectReference, QPointF, PDFObject>>& drawn = pagePlacements[pageIndex];
        const DrawnImageCallback addPlacement = [&drawn](const DrawnImage& image)
        {
            // An inline image is a part of the content, it is never replaced (R4-8)
            if (image.reference.isValid())
            {
                drawn.emplace_back(image.reference, image.inches, image.colorSpace);
            }
        };

        // The images of all layers (step 4 of phase 2, R4-4): an image of a layer hidden for the
        // export is changed too, the document must not stay in color when the layer is shown
        environment.processPageImages(PDFInteger(pageIndex), addPlacement, nullptr, operationControl);

        // A canceled walk reports nothing more, also from the other threads
        if (progress && !PDFOperationControl::isOperationCanceled(operationControl))
        {
            QMutexLocker locker(&progressMutex);
            if (!PDFOperationControl::isOperationCanceled(operationControl))
            {
                progress->step();
            }
        }
    };
    PDFImageCompressorHelper::executeForEachIndex(pageCount, operationControl, processPage);

    if (progress && pageCount > 0)
    {
        progress->finish();
    }

    std::map<PDFObjectReference, ImagePlacement> placements;
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        for (const auto& [reference, inches, colorSpace] : pagePlacements[pageIndex])
        {
            auto [iterator, inserted] = placements.try_emplace(reference);
            ImagePlacement& placement = iterator->second;
            if (inserted)
            {
                placement.reference = reference;
                placement.firstPageIndex = PDFInteger(pageIndex);
                placement.colorSpace = colorSpace;
            }
            placement.maximumInches.setX(qMax(placement.maximumInches.x(), inches.x()));
            placement.maximumInches.setY(qMax(placement.maximumInches.y(), inches.y()));
        }
    }

    result.reserve(placements.size());
    for (const auto& item : placements)
    {
        result.push_back(item.second);
    }
    return result;
}

std::optional<PDFImageCompressor::ImageStatistics> PDFImageCompressor::decodeImage(const Environment& environment, const PDFImage& image, PDFObjectReference reference, const ImagePlacement& placement, const PDFOperationControl* operationControl)
{
    PDFRenderErrorReporterDummy reporter;
    ImageStatistics statistics;
    statistics.reference = reference;
    statistics.image = image.getImage(environment.getCMS(), &reporter, operationControl);
    if (const TestDecodeHook& hook = PDFImageCompressorHelper::getTestDecodeHook())
    {
        statistics.image = hook(reference, std::move(statistics.image));
    }
    if (statistics.image.isNull())
    {
        return std::nullopt;
    }

    statistics.minimalDpi = QPointF(PDFImageCompressorHelper::getDpi(image.getImageData().getWidth(), placement.maximumInches.x()),
                                    PDFImageCompressorHelper::getDpi(image.getImageData().getHeight(), placement.maximumInches.y()));
    return statistics;
}

void PDFImageCompressor::setTestDecodeHook(TestDecodeHook hook)
{
    PDFImageCompressorHelper::getTestDecodeHook() = std::move(hook);
}

void PDFImageCompressor::visitImagesInParallel(const Environment& environment, const ImagePlacements& placements, const DecodedImageCallback& callback, const PDFOperationControl* operationControl) const
{
    const PDFDocument* document = environment.getDocument();
    if (!document || !document->getCatalog() || placements.empty())
    {
        return;
    }

    // The image of a page: the map is complete before the threads start, they only read it. The state
    // of an image is written by the one task, which has it.
    struct ImageState
    {
        const ImagePlacement* placement = nullptr;
        int part = 0;
        std::atomic<bool> isDone = false;
    };

    std::map<PDFObjectReference, ImageState> states;
    std::map<PDFInteger, int> imageCountOfPages;
    for (const ImagePlacement& placement : placements)
    {
        ImageState& state = states[placement.reference];
        state.placement = &placement;
        state.part = imageCountOfPages[placement.firstPageIndex]++;
    }

    // A task is a part of the images of a page: a page of many images runs in the threads too, each
    // part walks the content of its page once - at most one walk per thread, not one per image
    struct PagePart
    {
        PDFInteger pageIndex = 0;
        int part = 0;
        int partCount = 1;
    };

    const int taskCount = PDFImageCompressorHelper::getTaskCount();
    std::vector<PagePart> parts;
    for (const auto& [pageIndex, imageCount] : imageCountOfPages)
    {
        const int partCount = qMin(imageCount, taskCount);
        for (int part = 0; part < partCount; ++part)
        {
            parts.push_back({ pageIndex, part, partCount });
        }
    }

    const auto processPage = [&](size_t index)
    {
        const PagePart& pagePart = parts[index];
        const PDFInteger pageIndex = pagePart.pageIndex;

        // An image is taken on its first page only: its result does not depend on the threads
        const auto isOfThisPart = [&pagePart, pageIndex](const ImageState& state)
        {
            return state.placement->firstPageIndex == pageIndex && state.part % pagePart.partCount == pagePart.part && !state.isDone.load(std::memory_order_relaxed);
        };

        const DrawnImageCallback decode = [&](const DrawnImage& drawnImage)
        {
            const auto it = states.find(drawnImage.reference);
            if (it == states.end() || !isOfThisPart(it->second))
            {
                return;
            }

            if (PDFOperationControl::isOperationCanceled(operationControl))
            {
                return;
            }

            // An image, which cannot be created or decoded, is tried on its other pages
            std::optional<PDFImage> image = PDFImageCompressorHelper::createImage(document, drawnImage);
            if (!image)
            {
                return;
            }

            std::optional<ImageStatistics> statistics;
            try
            {
                statistics = decodeImage(environment, *image, drawnImage.reference, *it->second.placement, operationControl);
            }
            catch (const PDFException&)
            {
                return;
            }

            if (statistics)
            {
                it->second.isDone.store(true, std::memory_order_relaxed);
                callback(std::move(*statistics));
            }
        };

        // The images of all layers (R4-4), as collectImagePlacements found them
        environment.processPageImages(pageIndex, decode, nullptr, operationControl);
    };
    PDFImageCompressorHelper::executeForEachIndex(parts.size(), operationControl, processPage);

    if (PDFOperationControl::isOperationCanceled(operationControl))
    {
        return;
    }

    // An image, which gave no pixels on its first page, is tried on its other pages
    ImagePlacements rest;
    for (const ImagePlacement& placement : placements)
    {
        if (!states.at(placement.reference).isDone.load(std::memory_order_relaxed))
        {
            rest.push_back(placement);
        }
    }

    if (!rest.empty())
    {
        visitImages(environment, rest, callback, operationControl);
    }
}

void PDFImageCompressor::visitImages(const Environment& environment, const ImagePlacements& placements, const DecodedImageCallback& callback, const PDFOperationControl* operationControl) const
{
    const PDFDocument* document = environment.getDocument();
    if (!document || !document->getCatalog() || placements.empty())
    {
        return;
    }

    /// State of an image, which was not handed over yet
    enum class PendingState
    {
        Waiting,        ///< The image was not reached yet (or its data cannot be read)
        NoPixels        ///< The image was reached and gave no pixels, its other pages are tried
    };

    std::map<PDFObjectReference, PendingState> pending;
    std::map<PDFObjectReference, const ImagePlacement*> placementOfImage;
    std::set<PDFInteger> firstPages;
    for (const ImagePlacement& placement : placements)
    {
        pending[placement.reference] = PendingState::Waiting;
        placementOfImage[placement.reference] = &placement;
        firstPages.insert(placement.firstPageIndex);
    }

    const DrawnImageCallback decodeNext = [&](const DrawnImage& drawnImage)
    {
        const PDFObjectReference reference = drawnImage.reference;
        auto pendingIterator = pending.find(reference);
        if (pendingIterator == pending.end())
        {
            return;
        }

        // An image, whose data cannot be read, stays Waiting: it is not reported
        std::optional<PDFImage> image = PDFImageCompressorHelper::createImage(document, drawnImage);
        if (!image)
        {
            return;
        }

        pendingIterator->second = PendingState::NoPixels;

        std::optional<ImageStatistics> statistics;
        try
        {
            statistics = decodeImage(environment, *image, reference, *placementOfImage.at(reference), operationControl);
        }
        catch (const PDFException&)
        {
            return;
        }

        if (!statistics)
        {
            return;
        }

        pending.erase(pendingIterator);
        callback(std::move(*statistics));
    };

    // Pages, on which an image is drawn for the first time, are enough, until an
    // image of them gives no pixels - then its other pages are tried
    bool processAllPages = false;
    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    for (PDFInteger pageIndex = 0; pageIndex < pageCount && !pending.empty() && !PDFOperationControl::isOperationCanceled(operationControl); ++pageIndex)
    {
        if (!processAllPages && !firstPages.count(pageIndex))
        {
            continue;
        }

        // The images of all layers (R4-4), as collectImagePlacements found them
        environment.processPageImages(pageIndex, decodeNext, nullptr, operationControl);

        if (!processAllPages)
        {
            for (const auto& item : pending)
            {
                if (placementOfImage.at(item.first)->firstPageIndex <= pageIndex)
                {
                    processAllPages = true;
                    break;
                }
            }
        }
    }

    if (PDFOperationControl::isOperationCanceled(operationControl))
    {
        return;
    }

    // Images without pixels; an image, whose data cannot be read, is not reported
    for (const auto& item : pending)
    {
        if (item.second == PendingState::NoPixels)
        {
            ImageStatistics statistics;
            statistics.reference = item.first;
            statistics.minimalDpi = QPointF(0.0, 0.0);
            callback(std::move(statistics));
        }
    }
}

}   // namespace pdf
