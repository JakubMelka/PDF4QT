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

#ifndef PDFIMAGEOPTIMIZER_H
#define PDFIMAGEOPTIMIZER_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfimage.h"
#include "pdfimagecompressor.h"

#include <QImage>
#include <QPointF>
#include <QString>

#include <map>
#include <optional>
#include <vector>

namespace pdf
{
class PDFDocument;
class PDFRenderErrorReporter;
class PDFProgress;

struct PDFObjectReferenceLess
{
    bool operator()(const PDFObjectReference& left, const PDFObjectReference& right) const
    {
        if (left.objectNumber != right.objectNumber)
        {
            return left.objectNumber < right.objectNumber;
        }
        return left.generation < right.generation;
    }
};

class PDF4QTLIBCORESHARED_EXPORT PDFImageOptimizer
{
public:
    enum class OptimizationGoal
    {
        PreferQuality,
        MinimumSize
    };

    enum class ColorMode
    {
        Auto,
        Preserve,
        Color,
        Grayscale,
        Bitonal
    };

    enum class CompressionAlgorithm
    {
        Auto,
        Flate,
        JPEG,
        JPEG2000,
        RunLength,
        CCITTGroup4,
        JBIG2
    };

    struct CompressionProfile
    {
        CompressionAlgorithm algorithm = CompressionAlgorithm::Auto;
        int targetDpi = 0;
        PDFImage::ResampleFilter resampleFilter = PDFImage::ResampleFilter::Bicubic;
        int jpegQuality = 85;
        float jpeg2000Rate = 0.0f;
        int monochromeThreshold = -1; // <0 means automatic
        bool enablePngPredictor = true;
    };

    struct Settings
    {
        bool enabled = false;
        bool autoMode = true;
        ColorMode colorMode = ColorMode::Auto;
        OptimizationGoal goal = OptimizationGoal::PreferQuality;
        bool keepOriginalIfLarger = true;
        bool preserveTransparency = true;
        CompressionProfile colorProfile;
        CompressionProfile grayProfile;
        CompressionProfile bitonalProfile;

        static Settings createDefault();
    };

    struct ImageInfo
    {
        PDFObjectReference reference;
        QImage image;
        QPointF minimalDpi;
        QSize pixelSize;
        QString colorSpaceName;
        QString filterName;
        int bitsPerComponent = 0;
        bool hasSoftMask = false;
        bool hasTransparency = false;
        bool isImageMask = false;
        int originalBytes = 0;
    };

    struct ImageOverride
    {
        bool enabled = true;
        bool useCustomSettings = false;
        Settings settings;
    };

    using ImageOverrides = std::map<PDFObjectReference, ImageOverride, PDFObjectReferenceLess>;

    struct ImageAnalysis
    {
        bool grayscale = false;
        int uniqueColors = 0;
        double edgeDensity = 0.0;
        double variance = 0.0;
        bool hasTransparency = false;

        enum class Kind
        {
            Photo,
            LineArt,
            TextScan
        } kind = Kind::Photo;
    };

    struct ResolvedPlan
    {
        PDFImage::ImageEncodeOptions encodeOptions;
        CompressionAlgorithm algorithm = CompressionAlgorithm::Flate;
        ColorMode resolvedColorMode = ColorMode::Color;
        QSize targetSize;
        bool useSoftMask = false;
        bool hadUnsupportedCompression = false;
        int originalBytes = 0;
    };

    struct ImageResult
    {
        PDFObjectReference reference;
        bool keptOriginal = true;
        int originalBytes = 0;
        int newBytes = 0;
        QString message;
    };

    PDFImageOptimizer() = default;

    static std::vector<ImageInfo> collectImageInfos(const PDFDocument* document);
    static ImageAnalysis analyzeImage(const QImage& image);
    static ResolvedPlan resolvePlan(const ImageInfo& info, const Settings& settings);
    static QImage createPreviewImage(const ImageInfo& info, const ResolvedPlan& plan, bool simulateCompression);
    static int estimateEncodedBytes(const ImageInfo& info, const ResolvedPlan& plan, PDFRenderErrorReporter* reporter = nullptr);

    PDFDocument optimize(const PDFDocument* document,
                         const Settings& settings,
                         const ImageOverrides& overrides = ImageOverrides(),
                         PDFProgress* progress = nullptr,
                         PDFRenderErrorReporter* reporter = nullptr,
                         std::vector<ImageResult>* results = nullptr) const;
};

}   // namespace pdf

#endif // PDFIMAGEOPTIMIZER_H
