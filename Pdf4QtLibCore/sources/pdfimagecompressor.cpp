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
#include "pdfconstants.h"
#include "pdfdocument.h"
#include "pdfimage.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoptionalcontent.h"
#include "pdfpage.h"
#include "pdfpagecontentprocessor.h"
#include "pdffont.h"

#include <cmath>
#include <limits>
#include <map>

namespace pdf
{

/// Content processor, which only reports the image XObjects drawn on the page
class PDFImageCompressor::ImageCollectorProcessor : public PDFPageContentProcessor
{
public:
    ImageCollectorProcessor(const PDFPage* page,
                            const PDFDocument* document,
                            const PDFFontCache* fontCache,
                            const PDFCMS* cms,
                            const PDFOptionalContentActivity* optionalContentActivity,
                            const PDFMeshQualitySettings& meshQualitySettings,
                            const ImageCallback& callback) :
        PDFPageContentProcessor(page, document, fontCache, cms, optionalContentActivity, QTransform(), meshQualitySettings),
        m_callback(callback)
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

    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override
    {
        if (isContentSuppressed() || isProcessingCancelled())
        {
            return true;
        }

        if (!reference.isValid() || !stream)
        {
            // Inline images or direct streams without reference are skipped, as they can't be shared
            return true;
        }

        m_callback(image, stream, reference, calculateDpi(image));
        return true;
    }

private:
    QPointF calculateDpi(const PDFImage& image) const
    {
        const QTransform ctm = getGraphicState()->getCurrentTransformationMatrix();

        const auto axisLength = [](qreal x, qreal y)
        {
            const double length = std::hypot(static_cast<double>(x), static_cast<double>(y));
            return length * PDF_POINT_TO_INCH;
        };

        QPointF dpi(0.0, 0.0);

        const double widthInches = axisLength(ctm.m11(), ctm.m12());
        if (widthInches > std::numeric_limits<double>::epsilon())
        {
            dpi.setX(static_cast<double>(image.getImageData().getWidth()) / widthInches);
        }

        const double heightInches = axisLength(ctm.m21(), ctm.m22());
        if (heightInches > std::numeric_limits<double>::epsilon())
        {
            dpi.setY(static_cast<double>(image.getImageData().getHeight()) / heightInches);
        }

        return dpi;
    }

    const ImageCallback& m_callback;
};

PDFImageCompressor::Environment::Environment(const PDFDocument* document, PDFCMSPointer cms) :
    m_document(document),
    m_cms(std::move(cms)),
    m_optionalContentActivity(std::make_unique<PDFOptionalContentActivity>(document, OCUsage::Export, nullptr)),
    m_fontCache(std::make_unique<PDFFontCache>(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT))
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
    m_fontCacheShrinkGuard = std::make_unique<PDFFontCacheShrinkGuard>(m_fontCache.get(), this);
}

PDFImageCompressor::Environment::~Environment() = default;

void PDFImageCompressor::Environment::processPage(PDFInteger pageIndex, const ImageCallback& callback, const PDFOperationControl* operationControl) const
{
    const PDFCatalog* catalog = m_document ? m_document->getCatalog() : nullptr;
    const PDFPage* page = catalog && pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount() ? catalog->getPage(pageIndex) : nullptr;
    if (!page || PDFOperationControl::isOperationCancelled(operationControl))
    {
        return;
    }

    PDFMeshQualitySettings meshQualitySettings;
    ImageCollectorProcessor processor(page, m_document, m_fontCache.get(), m_cms.data(), m_optionalContentActivity.get(), meshQualitySettings, callback);
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

PDFImageCompressor::ImageStatisticsList PDFImageCompressor::collectImages(const PDFDocument* document) const
{
    ImageStatisticsList result;
    if (!document || !document->getCatalog())
    {
        return result;
    }

    Environment environment(document);
    std::map<PDFObjectReference, ImageStatistics> statistics;
    PDFRenderErrorReporterDummy reporter;

    auto updateStatistics = [&](const PDFImage& image, const PDFStream*, PDFObjectReference reference, QPointF dpi)
    {
        auto [iterator, inserted] = statistics.try_emplace(reference);
        ImageStatistics& stats = iterator->second;

        if (inserted)
        {
            stats.reference = reference;
        }

        if (stats.image.isNull())
        {
            stats.image = image.getImage(environment.getCMS(), &reporter, nullptr);
        }

        stats.minimalDpi.setX(updateAxisDpi(stats.minimalDpi.x(), dpi.x()));
        stats.minimalDpi.setY(updateAxisDpi(stats.minimalDpi.y(), dpi.y()));
    };

    const size_t pageCount = document->getCatalog()->getPageCount();
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        environment.processPage(PDFInteger(pageIndex), updateStatistics, nullptr);
    }

    result.reserve(statistics.size());
    for (auto& [reference, stats] : statistics)
    {
        if (!std::isfinite(stats.minimalDpi.x()))
        {
            stats.minimalDpi.setX(0.0);
        }
        if (!std::isfinite(stats.minimalDpi.y()))
        {
            stats.minimalDpi.setY(0.0);
        }
        result.push_back(std::move(stats));
    }

    return result;
}

}   // namespace pdf
