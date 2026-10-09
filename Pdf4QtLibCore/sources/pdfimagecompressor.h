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

#ifndef PDFIMAGECOMPRESSOR_H
#define PDFIMAGECOMPRESSOR_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfcms.h"

#include <QImage>
#include <QPointF>

#include <limits>
#include <memory>
#include <vector>
#include <functional>

namespace pdf
{
class PDFImage;
class PDFDocument;
class PDFFontCache;
class PDFFontCacheShrinkGuard;
class PDFOperationControl;
class PDFOptionalContentActivity;

/// Helper responsible for gathering image statistics needed for compression.
class PDF4QTLIBCORESHARED_EXPORT PDFImageCompressor
{
public:
    struct ImageStatistics
    {
        PDFObjectReference reference;
        QImage image;
        QPointF minimalDpi = QPointF(std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::infinity());
    };

    using ImageStatisticsList = std::vector<ImageStatistics>;

    /// Image XObject drawn on a page: the image, its stream, its reference and its
    /// resolution in the user space of the page per axis (0, if the axis is degenerated)
    using ImageCallback = std::function<void(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference, QPointF dpi)>;

    /// Resources of the content processor for the collection of the images drawn
    /// on the pages of a document (optional content, font cache, color management).
    /// The environment can be used by more threads processing different pages at once.
    class PDF4QTLIBCORESHARED_EXPORT Environment
    {
    public:
        /// \param document Processed document
        /// \param cms Color management system decoding the images; if null, the color
        ///        management system with the default settings is used
        explicit Environment(const PDFDocument* document, PDFCMSPointer cms = PDFCMSPointer());
        ~Environment();

        Environment(const Environment&) = delete;
        Environment& operator=(const Environment&) = delete;

        /// Calls the callback for every image XObject drawn on the page by its content,
        /// by forms and by tiling patterns. Inline images and direct streams are skipped,
        /// they are part of the content and cannot be shared or replaced.
        /// \param pageIndex Index of the page
        /// \param callback Callback called for every drawn image
        /// \param operationControl Cancellation (optional)
        void processPage(PDFInteger pageIndex, const ImageCallback& callback, const PDFOperationControl* operationControl) const;

        /// Returns the color management system decoding the images
        const PDFCMS* getCMS() const { return m_cms.data(); }

    private:
        const PDFDocument* m_document;
        PDFCMSPointer m_cms;
        std::unique_ptr<PDFOptionalContentActivity> m_optionalContentActivity;
        std::unique_ptr<PDFFontCache> m_fontCache;
        std::unique_ptr<PDFFontCacheShrinkGuard> m_fontCacheShrinkGuard;
    };

    /// Collects all image XObjects from the document and computes their
    /// analysis data required for compression decisions.
    /// \param document Processed document
    /// \return Collected statistics for every unique image reference
    ImageStatisticsList collectImages(const PDFDocument* document) const;

private:
    class ImageCollectorProcessor;

    /// Returns the smaller of the resolutions of an axis; values, which are not
    /// positive or finite, are ignored
    static double updateAxisDpi(double currentValue, double candidate);
};

}   // namespace pdf

#endif // PDFIMAGECOMPRESSOR_H
