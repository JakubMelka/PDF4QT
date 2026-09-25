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

#ifndef PDFOCRCOMPRESSION_H
#define PDFOCRCOMPRESSION_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QImage>
#include <QString>
#include <QJsonObject>
#include <QStringList>

#include <map>
#include <vector>

namespace pdf
{
class PDFDocument;
class PDFOperationControl;

/// Mode of the compression of the scanned images, which are written together
/// with the text layer (phase 3 of OCR_PLAN.md)
enum class PDFOCRCompressionMode
{
    Off,                ///< Images are not touched (default)
    Lossless,           ///< Bitonal images are re-encoded (JBIG2 generic region / CCITT G4), gray and color images by Flate; pixel identical
    BitonalTextScans,   ///< Gray and color scans of a text are converted to black and white (lossy), the others as in Lossless
    Custom              ///< Settings of the image optimizer (algorithm, JPEG quality, resolution), possibly lossy
};

/// Encoding of the black and white images
enum class PDFOCRBitonalEncoding
{
    Smallest,       ///< The smallest of JBIG2, CCITT G4 and Flate
    JBIG2,          ///< JBIG2, a single generic region (lossless, no symbol dictionary)
    CCITTGroup4,    ///< CCITT Group 4 (T.6)
    Flate           ///< Flate of the 1-bit samples
};

/// Method of the conversion of the scans of a text into black and white
enum class PDFOCRThresholdMethod
{
    Automatic,  ///< Global threshold by Otsu's method
    Adaptive,   ///< Adaptive local threshold (uneven lighting, stains)
    Manual      ///< Fixed threshold
};

/// Settings of the compression (part of the configuration and of the named profiles)
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRCompressionSettings
{
    PDFOCRCompressionMode mode = PDFOCRCompressionMode::Off;
    PDFOCRBitonalEncoding bitonalEncoding = PDFOCRBitonalEncoding::Smallest;
    PDFOCRThresholdMethod thresholdMethod = PDFOCRThresholdMethod::Automatic;

    /// Threshold of the manual method (0-255, pixels darker than it are black)
    int manualThreshold = 128;

    /// Compress also the images, which are drawn on pages, which are not written
    bool compressSharedImages = false;

    /// Custom mode: downsample images with a higher resolution to downsampleDpi
    bool downsample = false;
    int downsampleDpi = 300;

    /// Custom mode: quality of JPEG (the optimizer chooses JPEG for photos)
    int jpegQuality = 85;

    /// Images excluded by the user in the preview. They are references into the
    /// document of the current session, so they are not stored in the JSON.
    std::vector<PDFObjectReference> excludedImages;

    bool isEnabled() const { return mode != PDFOCRCompressionMode::Off; }

    /// Returns true, if the result may differ from the original pixels
    bool isLossy() const { return mode == PDFOCRCompressionMode::BitonalTextScans || mode == PDFOCRCompressionMode::Custom; }

    bool isExcluded(PDFObjectReference reference) const;

    QJsonObject toJson() const;
    static PDFOCRCompressionSettings fromJson(const QJsonObject& object);

    /// Validates the settings, returns the errors (translated)
    QStringList validate() const;

    static QString getModeName(PDFOCRCompressionMode mode);
    static QString getModeDescription(PDFOCRCompressionMode mode);
    static QString getBitonalEncodingName(PDFOCRBitonalEncoding encoding);
    static QString getThresholdMethodName(PDFOCRThresholdMethod method);

    bool operator==(const PDFOCRCompressionSettings&) const = default;
};

/// Result of the compression of a single image
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRCompressionImageResult
{
    enum class Action
    {
        Compressed,         ///< The image stream was replaced
        KeptLarger,         ///< The new encoding was not smaller, the original was kept
        SkippedShared,      ///< The image is drawn also on a page, which is not written
        SkippedExcluded,    ///< Excluded by the user
        SkippedUnsupported, ///< Image type which is not re-encoded (mask, transparency, color space, lossy source)
        Failed              ///< Decoding or encoding failed, the original was kept
    };

    /// Class of the content of the image
    enum class ImageClass
    {
        Bitonal,    ///< Black and white image
        TextScan,   ///< Gray or color scan of a text (mostly paper and ink)
        Picture,    ///< Photo, graphics or other content
        Unknown
    };

    PDFObjectReference reference;
    std::vector<PDFInteger> pages;
    Action action = Action::SkippedUnsupported;
    ImageClass imageClass = ImageClass::Unknown;
    QSize pixelSize;
    QString originalFilter;
    QString encoding;
    qint64 originalBytes = 0;
    qint64 newBytes = 0;
    QString message;

    static QString getActionName(Action action);
    static QString getImageClassName(ImageClass imageClass);
};

/// Report of the compression
struct PDF4QTLIBCORESHARED_EXPORT PDFOCRCompressionReport
{
    std::vector<PDFOCRCompressionImageResult> images;

    /// The version of the document was raised to 1.4 (required by JBIG2)
    bool versionRaised = false;

    /// Maximal number of images, which were decoded at the same time (diagnostics of
    /// the streaming processing: it is bounded by the memory budget, not by the number
    /// of the pages)
    int maximumConcurrentImages = 0;

    int getCount(PDFOCRCompressionImageResult::Action action) const;

    /// Total size of the streams of the compressed images before and after
    qint64 getOriginalBytes() const;
    qint64 getNewBytes() const;

    bool isChanged() const { return getCount(PDFOCRCompressionImageResult::Action::Compressed) > 0; }

    /// Returns the human readable summary: "Images: 12 compressed, 1 kept (larger),
    /// 2 skipped (shared). Size 84.2 MB -> 12.6 MB."
    QString getSummary() const;

    /// Formats the size in bytes (kB, MB)
    static QString formatBytes(qint64 bytes);
};

/// Compression of the scanned images of the pages, where the OCR text layer is
/// written. The images are processed one by one: an image is decoded, encoded
/// and freed immediately, so the memory does not grow with the number of pages.
/// The placement of the images is never changed (only the image streams), so
/// the geometry of the text layer stays valid.
class PDF4QTLIBCORESHARED_EXPORT PDFOCRImageCompressor
{
public:
    /// Compresses the images drawn on the pages. Returns the document with the
    /// replaced image streams; if nothing is compressed, a copy of the document.
    /// \param document Source document (not modified)
    /// \param pages Pages, whose images are compressed
    /// \param settings Settings
    /// \param memoryBudget Budget of the decoded images in bytes (bounds the parallelism)
    /// \param operationControl Cancellation
    /// \param report Report (optional)
    static PDFDocument compress(const PDFDocument* document,
                                const std::vector<PDFInteger>& pages,
                                const PDFOCRCompressionSettings& settings,
                                qint64 memoryBudget,
                                const PDFOperationControl* operationControl,
                                PDFOCRCompressionReport* report);

    /// Preview of the compression of one image of a page
    struct Preview
    {
        PDFOCRCompressionImageResult result;
        QImage original;
        QImage compressed;  ///< Image as it will look after the compression (null, if not compressed)
    };

    /// Creates the preview of the compression of the images of the page: the decoded
    /// original, the result and the sizes. Nothing is written into the document.
    static std::vector<Preview> createPreview(const PDFDocument* document,
                                              PDFInteger pageIndex,
                                              const std::vector<PDFInteger>& processedPages,
                                              const PDFOCRCompressionSettings& settings,
                                              const PDFOperationControl* operationControl);

    /// Returns true, if the image is a scan of a text: mostly paper and ink with
    /// only a few mid tones and almost no color
    static bool isTextScan(const QImage& image);

    /// Converts the image into black and white by the method of the settings
    static QImage toBitonal(const QImage& image, const PDFOCRCompressionSettings& settings, const PDFOperationControl* operationControl);

    /// Returns the image references drawn (as XObjects) on the pages, and for every
    /// image the pages, which have it in their resources. Form XObjects are searched
    /// recursively. Used to detect the images shared with the other pages.
    static std::map<PDFObjectReference, std::vector<PDFInteger>> getImageUsage(const PDFDocument* document);
};

}   // namespace pdf

#endif // PDFOCRCOMPRESSION_H
