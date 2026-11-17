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

#include "pdfoutputpreviewanalyzer.h"
#include "pdfcatalog.h"
#include "pdfconstants.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"

#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pdf
{

/// The helpers of the analyzer (file local)
class PDFOutputPreviewAnalyzerHelper
{
public:
    /// Tolerance of the comparisons of a value in % with a limit (the float rounding of the bitmap:
    /// 0.95 of K is 94.99999 % - it is still at the limit 95 %)
    static constexpr double LIMIT_TOLERANCE = 1.0e-3;

    /// The mask of the channels of the bitmap of the checked inks of the result
    static uint32_t getActiveMask(const PDFOutputPreviewResult& result)
    {
        uint32_t mask = 0;
        for (const PDFOutputPreviewInk& ink : result.inks)
        {
            if (ink.isActive && ink.channel >= 0 && ink.channel < 32)
            {
                mask |= (uint32_t(1) << ink.channel);
            }
        }
        return mask;
    }

    /// The mask of the channels of the bitmap of all inks of the result (checked or not)
    static uint32_t getChannelMask(const PDFOutputPreviewResult& result)
    {
        uint32_t mask = 0;
        for (const PDFOutputPreviewInk& ink : result.inks)
        {
            if (ink.channel >= 0 && ink.channel < 32)
            {
                mask |= (uint32_t(1) << ink.channel);
            }
        }
        return mask;
    }

    /// The values of the color channels of a pixel weighted by its opacity (the ink left on the paper)
    static void getInkValues(const PDFFloatBitmap& bitmap, size_t x, size_t y, std::vector<PDFColorComponent>& values)
    {
        const PDFPixelFormat format = bitmap.getPixelFormat();
        const PDFConstColorBuffer pixel = bitmap.getPixel(x, y);
        const PDFColorComponent alpha = format.hasOpacityChannel() ? pixel[format.getOpacityChannelIndex()] : 1.0f;
        const uint8_t colorCount = format.getColorChannelCount();
        values.resize(colorCount);
        for (uint8_t i = 0; i < colorCount; ++i)
        {
            values[i] = qBound<PDFColorComponent>(0.0f, pixel[i], 1.0f) * qBound<PDFColorComponent>(0.0f, alpha, 1.0f);
        }
    }

    /// The total area coverage of the checked inks in % (R7-22 (2): the spot inks too)
    static double getTotalCoverage(const std::vector<PDFColorComponent>& values, uint32_t activeMask)
    {
        double total = 0.0;
        for (size_t i = 0; i < values.size() && i < 32; ++i)
        {
            if (activeMask & (uint32_t(1) << i))
            {
                total += values[i];
            }
        }
        return total * 100.0;
    }

    /// Rich black (owner answer Q7.7): K at least \p limit % and C, M or Y above
    /// PDFOutputPreviewAnalyzer::PRESENT_INK_TINT, of the checked process inks; only with four
    /// process inks (C, M, Y, K)
    static bool isRichBlack(const std::vector<PDFColorComponent>& values, uint8_t processCount, uint32_t activeMask, double limit)
    {
        if (processCount != 4 || values.size() < 4)
        {
            return false;
        }

        auto getValue = [&values, activeMask](size_t index) -> double
        {
            return (activeMask & (uint32_t(1) << index)) ? double(values[index]) : 0.0;
        };

        const double black = getValue(3) * 100.0;
        const bool hasOtherInk = getValue(0) > PDFOutputPreviewAnalyzer::PRESENT_INK_TINT ||
                                 getValue(1) > PDFOutputPreviewAnalyzer::PRESENT_INK_TINT ||
                                 getValue(2) > PDFOutputPreviewAnalyzer::PRESENT_INK_TINT;
        return hasOtherInk && black >= limit - LIMIT_TOLERANCE;
    }

    /// Area of a rectangle of the page in mm² with /UserUnit
    static double getAreaMM(const PDFPage* page, const QRectF& rect)
    {
        const QRectF rectMM = page->getPhysicalRectMM(rect);
        return qAbs(rectMM.width() * rectMM.height());
    }

    /// A copy of the picture of the separations in a format, whose pixels can be set
    static QImage createWarningBase(const PDFOutputPreviewResult& result)
    {
        return result.image.convertToFormat(QImage::Format_RGBA8888);
    }
};

PDFOutputPreviewSettings PDFOutputPreviewSettings::create(const PDFDocument* document, const PDFDocumentToolRenderContext& rendering)
{
    PDFOutputPreviewSettings settings;
    settings.rendering = rendering;

    if (rendering.cmsSettings)
    {
        settings.cmsSettings = rendering.cmsSettings->withoutProofing();
    }
    else
    {
        PDFCMSManager manager(nullptr);
        manager.setDocument(document);
        settings.cmsSettings = manager.getDefaultSettings();
    }

    return settings;
}

std::vector<PDFOutputPreviewInk> PDFOutputPreviewAnalyzer::createInks(const PDFInkMapper* inkMapper, const PDFOutputPreviewSettings& settings)
{
    std::vector<PDFOutputPreviewInk> inks;
    if (!inkMapper)
    {
        return inks;
    }

    const std::vector<PDFInkMapper::ColorInfo> separations = inkMapper->getSeparations(4, true);
    for (size_t i = 0; i < separations.size(); ++i)
    {
        const PDFInkMapper::ColorInfo& info = separations[i];

        PDFOutputPreviewInk ink;
        ink.name = info.name;
        ink.displayName = info.textName;
        ink.color = info.color;
        ink.isSpot = info.isSpot;
        ink.channel = int(i);
        ink.isActive = std::find(settings.inactiveInks.cbegin(), settings.inactiveInks.cend(), info.name) == settings.inactiveInks.cend();
        inks.push_back(std::move(ink));
    }

    if (inkMapper->getActiveSpotColorCount() > 0)
    {
        for (const PDFInkMapper::ColorInfo& info : inkMapper->getSpotColors())
        {
            if (!info.canBeActive)
            {
                PDFOutputPreviewInk ink;
                ink.name = info.name;
                ink.displayName = info.textName;
                ink.color = info.color;
                ink.isSpot = true;
                ink.isActive = false;
                ink.channel = -1;
                inks.push_back(std::move(ink));
            }
        }
    }

    return inks;
}

PDFOutputPreviewEnvironment::PDFOutputPreviewEnvironment(const PDFDocument* document, const PDFOutputPreviewSettings& settings, const std::vector<PDFInteger>& spotPages, const PDFOperationControl* control) :
    m_document(document)
{
    // Q7.8 (a): the layers as printing (8.11.4.4, the /Print usage), Q7.10: never a proof
    m_optionalContentActivity = std::make_unique<PDFOptionalContentActivity>(document, OCUsage::Print, nullptr);

    m_cmsManager = std::make_unique<PDFCMSManager>(nullptr);
    m_cmsManager->setDocument(document);
    m_cmsManager->setSettings(settings.cmsSettings.withoutProofing());

    const PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), m_optionalContentActivity.get());
    m_fontCache = std::make_unique<PDFFontCache>(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    m_fontCache->setDocument(modifiedDocument);
    m_fontCache->setCacheShrinkEnabled(nullptr, false);

    m_iccProfileCache = std::make_unique<PDFICCProfileCache>();
    m_iccProfileCache->setDocument(modifiedDocument);

    m_inkMapper = std::make_unique<PDFInkMapper>(m_cmsManager.get(), document);
    if (document)
    {
        m_inkMapper->createSpotColors(settings.isSeparationSimulated, spotPages, control);
    }
}

PDFOutputPreviewEnvironment::~PDFOutputPreviewEnvironment()
{
    // The ink mapper refers to the color management: it dies first
    m_inkMapper.reset();
    m_fontCache->setCacheShrinkEnabled(nullptr, true);
}

PDFOutputPreviewContext PDFOutputPreviewEnvironment::getContext() const
{
    PDFOutputPreviewContext context;
    context.document = m_document;
    context.fontCache = m_fontCache.get();
    context.iccProfileCache = m_iccProfileCache.get();
    context.cmsManager = m_cmsManager.get();
    context.optionalContentActivity = m_optionalContentActivity.get();
    context.inkMapper = m_inkMapper.get();
    return context;
}

PDFOutputPreviewResult PDFOutputPreviewAnalyzer::render(const PDFDocument* document, PDFInteger pageIndex, const PDFOutputPreviewSettings& settings, const PDFOperationControl* control)
{
    if (!document)
    {
        PDFOutputPreviewResult result;
        result.pageIndex = pageIndex;
        result.errorMessage = tr("There is no document.");
        return result;
    }

    // The inks of the whole document: the list of the inks is the same on every page (as the
    // old Output Preview of the Editor)
    std::vector<PDFInteger> pages(document->getCatalog()->getPageCount(), 0);
    std::iota(pages.begin(), pages.end(), PDFInteger(0));

    const PDFOutputPreviewEnvironment environment(document, settings, pages, control);
    return renderPage(environment.getContext(), pageIndex, settings, control);
}

QSize PDFOutputPreviewAnalyzer::getImageSize(const PDFPage* page, int resolution)
{
    if (!page)
    {
        return QSize();
    }

    const int boundedResolution = qBound(72, resolution, PDFOutputPreviewSettings::MAXIMUM_RESOLUTION);
    QSizeF pageSize = page->getRotatedMediaBox().size();
    if (!(pageSize.width() > 0.0) || !(pageSize.height() > 0.0) || !std::isfinite(pageSize.width()) || !std::isfinite(pageSize.height()))
    {
        return QSize();
    }

    pageSize.scale(boundedResolution, boundedResolution, Qt::KeepAspectRatio);
    const QSize imageSize(qMax(1, qRound(pageSize.width())), qMax(1, qRound(pageSize.height())));
    return imageSize;
}

qint64 PDFOutputPreviewAnalyzer::getMemoryEstimate(const PDFPage* page, int resolution, size_t spotInkCount)
{
    const QSize imageSize = getImageSize(page, resolution);
    if (imageSize.isEmpty())
    {
        return 0;
    }

    // Channels of a pixel (float): the device group (RGB, spots, shape, opacity, a word of the
    // active colors) twice (initial and immediate backdrop), the page group (CMYK, spots, shape,
    // opacity, the word) twice, the draw buffer, the conversion of the group and the original
    // image, the soft mask and the picture
    const qint64 spots = qint64(qMin<size_t>(spotInkCount, PDFInkMapper::MAX_SPOT_COLOR_COMPONENTS));
    const qint64 deviceChannels = 3 + spots + 3;
    const qint64 pageChannels = 4 + spots + 3;
    const qint64 channels = 2 * deviceChannels + 4 * pageChannels + 3;
    return qint64(imageSize.width()) * qint64(imageSize.height()) * channels * qint64(sizeof(PDFColorComponent));
}

PDFOutputPreviewResult PDFOutputPreviewAnalyzer::renderPage(const PDFOutputPreviewContext& context, PDFInteger pageIndex, const PDFOutputPreviewSettings& settings, const PDFOperationControl* control)
{
    using Helper = PDFOutputPreviewAnalyzerHelper;

    PDFOutputPreviewResult result;
    result.pageIndex = pageIndex;

    const PDFDocument* document = context.document;
    if (!document || !context.inkMapper || !context.cmsManager || !context.fontCache || !context.optionalContentActivity)
    {
        result.errorMessage = tr("There is no document.");
        return result;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFPage* page = (pageIndex >= 0 && pageIndex < PDFInteger(catalog->getPageCount())) ? catalog->getPage(pageIndex) : nullptr;
    if (!page)
    {
        result.errorMessage = tr("Page %1 does not exist.").arg(pageIndex + 1);
        return result;
    }

    const QSize imageSize = getImageSize(page, settings.resolution);
    if (imageSize.isEmpty())
    {
        result.errorMessage = tr("Page %1 has no area.").arg(pageIndex + 1);
        return result;
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        result.isCanceled = true;
        return result;
    }

    // The inks: the process inks and the active spot inks are the channels of the bitmap in this
    // order; the spot inks over the limit have no channel (476:11703)
    const PDFInkMapper* inkMapper = context.inkMapper;
    result.inks = createInks(inkMapper, settings);
    const uint32_t activeColorMask = Helper::getActiveMask(result) | ~Helper::getChannelMask(result);
    const bool hasActiveSpots = inkMapper->getActiveSpotColorCount() > 0;
    result.isSpotLimitReached = hasActiveSpots && inkMapper->isSpotColorLimitReached();

    PDFTransparencyRendererSettings rendererSettings;
    rendererSettings.flags = PDFTransparencyRendererSettings::None;
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::DisplayImages, settings.content & uint32_t(PDFOutputPreviewContent::Images));
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::DisplayText, settings.content & uint32_t(PDFOutputPreviewContent::Text));
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::DisplayVectorGraphics, settings.content & uint32_t(PDFOutputPreviewContent::Vectors));
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::DisplayShadings, settings.content & uint32_t(PDFOutputPreviewContent::Shadings));
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::DisplayTilingPatterns, settings.content & uint32_t(PDFOutputPreviewContent::Patterns));
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::SaveOriginalProcessImage, true);
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::OriginalProcessImageWithoutMask, true);
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::ActiveColorMask, activeColorMask != PDFPixelFormat::getAllColorsMask());
    rendererSettings.flags.setFlag(PDFTransparencyRendererSettings::SeparationSimulation, hasActiveSpots);
    rendererSettings.activeColorMask = activeColorMask;

    const QTransform pagePointToDevicePoint = PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0.0, 0.0), QSizeF(imageSize)));
    const PDFCMSPointer cms = context.cmsManager->getCurrentCMS();

    QList<PDFRenderError> errors;
    try
    {
        PDFTransparencyRenderer renderer(page, document, context.fontCache, context.iccProfileCache, cms.data(), context.optionalContentActivity,
                                         inkMapper, rendererSettings, pagePointToDevicePoint);
        renderer.setOperationControl(control);
        renderer.beginPaint(imageSize);
        errors = renderer.processContents();

        // Q7.8 (a): the annotations, which are printed
        if (!PDFOperationControl::isOperationCanceled(control))
        {
            errors.append(renderer.processPrintedAnnotations());
        }

        renderer.endPaint();

        if (PDFOperationControl::isOperationCanceled(control))
        {
            result.isCanceled = true;
            return result;
        }

        const QColor paperColor = settings.isPaperColorSimulated && settings.paperColor.isValid() ? settings.paperColor : QColor(Qt::white);
        result.image = renderer.toImage(false, true, PDFRGB{ PDFColorComponent(paperColor.redF()), PDFColorComponent(paperColor.greenF()), PDFColorComponent(paperColor.blueF()) });
        result.bitmap = renderer.getOriginalProcessBitmap();
    }
    catch (const PDFException& exception)
    {
        result.errorMessage = exception.getMessage();
        return result;
    }
    catch (const PDFRendererException& exception)
    {
        result.errorMessage = exception.getError().message;
        return result;
    }

    // 476:11358: the problems of the page, each text once (no information). A set of the texts: a hostile
    // content stream has millions of distinct errors (an unknown operator of another name each), a search
    // in the list for each of them would never end
    QSet<QString> knownIssues;
    for (const PDFRenderError& error : errors)
    {
        if (error.type == RenderErrorType::Information)
        {
            continue;
        }

        if (!knownIssues.contains(error.message))
        {
            knownIssues.insert(error.message);
            result.renderingIssues.push_back(error);
        }
    }

    const PDFFloatBitmapWithColorSpace& bitmap = result.bitmap;
    const size_t width = bitmap.getWidth();
    const size_t height = bitmap.getHeight();
    if (width == 0 || height == 0)
    {
        result.errorMessage = tr("Page %1 cannot be rendered.").arg(pageIndex + 1);
        return result;
    }

    // Q7.6 (b), R7-8: the box of the ratios is the TrimBox, else the CropBox, clipped by the
    // MediaBox (ISO 32000-2, 14.11.2); TrimBox defaults to the CropBox, so only the key tells
    // (TrimBox is not inherited, Table 31: the key of the page dictionary itself)
    const PDFDictionary* pageDictionary = document->getDictionaryFromObject(document->getObjectByReference(page->getPageReference()));
    result.isTrimBox = pageDictionary && pageDictionary->hasKey("TrimBox");
    const QRectF ratioBox = result.isTrimBox ? page->getEffectiveTrimBox() : page->getEffectiveCropBox();
    result.pageArea = Helper::getAreaMM(page, ratioBox);
    result.mediaArea = Helper::getAreaMM(page, page->getMediaBox());
    const QRectF deviceRatioBox = pagePointToDevicePoint.mapRect(ratioBox);
    const double pixelArea = result.mediaArea / (double(width) * double(height));

    const PDFPixelFormat format = bitmap.getPixelFormat();
    const uint8_t colorCount = format.getColorChannelCount();
    const uint8_t processCount = format.getProcessColorChannelCount();
    const uint32_t activeMask = Helper::getActiveMask(result);

    std::vector<double> areaSums(colorCount, 0.0);
    std::vector<double> boxSums(colorCount, 0.0);
    std::vector<PDFColorComponent> values;
    qint64 boxPixels = 0;
    qint64 overLimitPixels = 0;
    qint64 richBlackPixels = 0;
    double maximumCoverage = 0.0;

    for (size_t y = 0; y < height; ++y)
    {
        const double centerY = double(y) + 0.5;
        const bool isRowInside = centerY >= deviceRatioBox.top() && centerY < deviceRatioBox.bottom();

        for (size_t x = 0; x < width; ++x)
        {
            const double centerX = double(x) + 0.5;
            const bool isInside = isRowInside && centerX >= deviceRatioBox.left() && centerX < deviceRatioBox.right();

            Helper::getInkValues(bitmap, x, y, values);
            for (uint8_t i = 0; i < colorCount; ++i)
            {
                areaSums[i] += values[i];
                if (isInside)
                {
                    boxSums[i] += values[i];
                }
            }

            if (isInside)
            {
                ++boxPixels;
            }

            const double coverage = Helper::getTotalCoverage(values, activeMask);
            maximumCoverage = qMax(maximumCoverage, coverage);
            if (coverage > settings.coverageLimit + Helper::LIMIT_TOLERANCE)
            {
                ++overLimitPixels;
            }

            if (Helper::isRichBlack(values, processCount, activeMask, settings.richBlackLimit))
            {
                ++richBlackPixels;
            }
        }
    }

    for (PDFOutputPreviewInk& ink : result.inks)
    {
        if (ink.channel >= 0 && ink.channel < int(colorCount))
        {
            ink.area = areaSums[size_t(ink.channel)] * pixelArea;
            ink.ratio = boxPixels > 0 ? boxSums[size_t(ink.channel)] / double(boxPixels) * 100.0 : 0.0;
        }
    }

    result.maximumCoverage = maximumCoverage;
    result.areaOverCoverageLimit = double(overLimitPixels) * pixelArea;
    result.richBlackArea = double(richBlackPixels) * pixelArea;
    return result;
}

QImage PDFOutputPreviewAnalyzer::createImage(const PDFOutputPreviewResult& result, Mode mode, const PDFOutputPreviewSettings& settings, const QColor& warningColor)
{
    using Helper = PDFOutputPreviewAnalyzerHelper;

    const PDFFloatBitmapWithColorSpace& bitmap = result.bitmap;
    if (!result.isRendered() || bitmap.getWidth() == 0 || bitmap.getHeight() == 0 || result.image.isNull())
    {
        return QImage();
    }

    const PDFPixelFormat format = bitmap.getPixelFormat();
    const uint32_t activeMask = Helper::getActiveMask(result);
    const int width = int(qMin<size_t>(bitmap.getWidth(), size_t(result.image.width())));
    const int height = int(qMin<size_t>(bitmap.getHeight(), size_t(result.image.height())));
    std::vector<PDFColorComponent> values;

    switch (mode)
    {
        case Mode::Separations:
            return result.image;

        case Mode::CoverageWarning:
        case Mode::RichBlackWarning:
        {
            QImage image = Helper::createWarningBase(result);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    Helper::getInkValues(bitmap, size_t(x), size_t(y), values);
                    const bool isWarning = (mode == Mode::CoverageWarning) ? Helper::getTotalCoverage(values, activeMask) > settings.coverageLimit + Helper::LIMIT_TOLERANCE
                                                                           : Helper::isRichBlack(values, format.getProcessColorChannelCount(), activeMask, settings.richBlackLimit);
                    if (isWarning)
                    {
                        image.setPixelColor(x, y, warningColor);
                    }
                }
            }
            return image;
        }

        case Mode::InkCoverage:
        {
            // The scale from no ink to the highest coverage of the page (at least 100 %)
            const PDFColorScale colorScale(0.0, qMax(100.0, result.maximumCoverage));
            QImage image(width, height, QImage::Format_RGBX8888);
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    Helper::getInkValues(bitmap, size_t(x), size_t(y), values);
                    image.setPixelColor(x, y, colorScale.map(Helper::getTotalCoverage(values, activeMask)));
                }
            }
            return image;
        }

        case Mode::ShapeChannel:
            return format.hasShapeChannel() ? bitmap.getChannelImage(format.getShapeChannelIndex()) : QImage();

        case Mode::OpacityChannel:
            return format.hasOpacityChannel() ? bitmap.getChannelImage(format.getOpacityChannelIndex()) : QImage();
    }

    return QImage();
}

std::vector<double> PDFOutputPreviewAnalyzer::getValuesAt(const PDFOutputPreviewResult& result, QPoint pixel)
{
    const PDFFloatBitmapWithColorSpace& bitmap = result.bitmap;
    if (pixel.x() < 0 || pixel.y() < 0 || size_t(pixel.x()) >= bitmap.getWidth() || size_t(pixel.y()) >= bitmap.getHeight())
    {
        return { };
    }

    std::vector<PDFColorComponent> values;
    PDFOutputPreviewAnalyzerHelper::getInkValues(bitmap, size_t(pixel.x()), size_t(pixel.y()), values);

    std::vector<double> tints;
    tints.reserve(result.inks.size());
    for (const PDFOutputPreviewInk& ink : result.inks)
    {
        tints.push_back(ink.channel >= 0 && size_t(ink.channel) < values.size() ? double(values[size_t(ink.channel)]) * 100.0 : 0.0);
    }
    return tints;
}

QString PDFOutputPreviewAnalyzer::getModeId(Mode mode)
{
    switch (mode)
    {
        case Mode::Separations:
            return QStringLiteral("separations");
        case Mode::CoverageWarning:
            return QStringLiteral("coverage-warning");
        case Mode::RichBlackWarning:
            return QStringLiteral("rich-black-warning");
        case Mode::InkCoverage:
            return QStringLiteral("ink-coverage");
        case Mode::ShapeChannel:
            return QStringLiteral("shape-channel");
        case Mode::OpacityChannel:
            return QStringLiteral("opacity-channel");
    }
    return QStringLiteral("separations");
}

PDFOutputPreviewAnalyzer::Mode PDFOutputPreviewAnalyzer::getMode(const QString& id)
{
    for (const Mode mode : { Mode::Separations, Mode::CoverageWarning, Mode::RichBlackWarning, Mode::InkCoverage, Mode::ShapeChannel, Mode::OpacityChannel })
    {
        if (getModeId(mode) == id)
        {
            return mode;
        }
    }
    return Mode::Separations;
}

}   // namespace pdf
