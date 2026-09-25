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

#include "pdfscanpreparation.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfdocumentbuilder.h"
#include "pdfrenderer.h"
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdfexception.h"
#include "pdfconstants.h"

#include <QtMath>

#include <set>
#include <deque>
#include <cmath>
#include <algorithm>

namespace pdf
{

namespace
{

/// Otsu's threshold of the histogram of a gray image
int computeOtsuThreshold(const QImage& gray)
{
    std::array<qint64, 256> histogram = { };
    for (int y = 0; y < gray.height(); ++y)
    {
        const uchar* row = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
        {
            ++histogram[row[x]];
        }
    }

    const qint64 total = qint64(gray.width()) * gray.height();
    double sum = 0.0;
    for (int i = 0; i < 256; ++i)
    {
        sum += double(i) * histogram[size_t(i)];
    }

    double sumBackground = 0.0;
    qint64 weightBackground = 0;
    double bestVariance = -1.0;
    int threshold = 128;
    for (int i = 0; i < 256; ++i)
    {
        weightBackground += histogram[size_t(i)];
        if (weightBackground == 0)
        {
            continue;
        }

        const qint64 weightForeground = total - weightBackground;
        if (weightForeground == 0)
        {
            break;
        }

        sumBackground += double(i) * histogram[size_t(i)];
        const double meanBackground = sumBackground / weightBackground;
        const double meanForeground = (sum - sumBackground) / weightForeground;
        const double variance = double(weightBackground) * double(weightForeground) * (meanBackground - meanForeground) * (meanBackground - meanForeground);
        if (variance > bestVariance)
        {
            bestVariance = variance;
            threshold = i;
        }
    }

    return threshold;
}

/// Marks the dark pixels, which are not connected with the border of the image
/// (the edges of the scanner and the shadows of the page are connected with it)
std::vector<uchar> computeInk(const QImage& gray, int threshold, int* borderPixels)
{
    const int width = gray.width();
    const int height = gray.height();
    std::vector<uchar> dark(size_t(width) * height, 0);
    for (int y = 0; y < height; ++y)
    {
        const uchar* row = gray.constScanLine(y);
        for (int x = 0; x < width; ++x)
        {
            dark[size_t(y) * width + x] = row[x] <= threshold ? 1 : 0;
        }
    }

    // Flood fill of the dark regions touching the border (value 2)
    std::deque<std::pair<int, int>> queue;
    auto push = [&](int x, int y)
    {
        uchar& value = dark[size_t(y) * width + x];
        if (value == 1)
        {
            value = 2;
            queue.emplace_back(x, y);
        }
    };

    for (int x = 0; x < width; ++x)
    {
        push(x, 0);
        push(x, height - 1);
    }
    for (int y = 0; y < height; ++y)
    {
        push(0, y);
        push(width - 1, y);
    }

    int border = 0;
    while (!queue.empty())
    {
        const auto [x, y] = queue.front();
        queue.pop_front();
        ++border;
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < width && ny < height)
                {
                    push(nx, ny);
                }
            }
        }
    }

    if (borderPixels)
    {
        *borderPixels = border;
    }

    for (uchar& value : dark)
    {
        value = value == 1 ? 1 : 0;
    }
    return dark;
}

/// Finds the gutter of a spread in the middle band of the axis. Columns (or rows)
/// of the image are profiled: a wide gap without ink, or a dark valley of the
/// mean brightness (the shadow of the spine).
std::optional<PDFScanPreparation::GutterCandidate> findGutter(const QImage& gray, const std::vector<uchar>& ink, bool vertical, double dpi)
{
    const int width = gray.width();
    const int height = gray.height();
    const int length = vertical ? width : height;     // Along the axis of the split position
    const int breadth = vertical ? height : width;    // Along the split line
    if (length < 20 || breadth < 20)
    {
        return std::nullopt;
    }

    const int bandStart = int(length * 0.35);
    const int bandEnd = int(length * 0.65);
    std::vector<int> inkCounts;
    std::vector<double> brightness;
    for (int position = bandStart; position < bandEnd; ++position)
    {
        int count = 0;
        double sum = 0.0;
        for (int i = 0; i < breadth; ++i)
        {
            const int x = vertical ? position : i;
            const int y = vertical ? i : position;
            count += ink[size_t(y) * width + x];
            sum += gray.constScanLine(y)[x];
        }
        inkCounts.push_back(count);
        brightness.push_back(sum / breadth);
    }

    // White gap: the widest run of the positions without ink
    const int allowedInk = qMax(1, breadth / 500);
    int bestRunStart = -1;
    int bestRunLength = 0;
    for (int i = 0; i < int(inkCounts.size());)
    {
        if (inkCounts[size_t(i)] > allowedInk)
        {
            ++i;
            continue;
        }

        int j = i;
        while (j < int(inkCounts.size()) && inkCounts[size_t(j)] <= allowedInk)
        {
            ++j;
        }
        if (j - i > bestRunLength)
        {
            bestRunLength = j - i;
            bestRunStart = i;
        }
        i = j;
    }

    std::optional<PDFScanPreparation::GutterCandidate> gap;
    const int minimalRun = qMax(3, length / 100);
    if (bestRunStart >= 0 && bestRunLength >= minimalRun && bestRunLength < int(inkCounts.size()))
    {
        PDFScanPreparation::GutterCandidate candidate;
        candidate.position = (bandStart + bestRunStart + bestRunLength * 0.5) / dpi * 72.0;
        candidate.confidence = qBound(0.0, 100.0 * bestRunLength / (length * 0.04), 100.0);
        gap = candidate;
    }

    // Shadow of the spine: a valley of the smoothed brightness
    std::vector<double> smoothed(brightness.size(), 0.0);
    for (size_t i = 0; i < brightness.size(); ++i)
    {
        double sum = 0.0;
        int count = 0;
        for (int k = -3; k <= 3; ++k)
        {
            const int index = int(i) + k;
            if (index >= 0 && index < int(brightness.size()))
            {
                sum += brightness[size_t(index)];
                ++count;
            }
        }
        smoothed[i] = sum / count;
    }

    std::optional<PDFScanPreparation::GutterCandidate> shadow;
    if (!smoothed.empty())
    {
        std::vector<double> sorted = smoothed;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const double median = sorted[sorted.size() / 2];
        const auto minimum = std::min_element(smoothed.begin(), smoothed.end());
        const double depth = median - *minimum;
        if (depth > 25.0)
        {
            PDFScanPreparation::GutterCandidate candidate;
            candidate.position = (bandStart + double(std::distance(smoothed.begin(), minimum)) + 0.5) / dpi * 72.0;
            candidate.confidence = qBound(0.0, depth * 2.0, 100.0);
            candidate.isShadow = true;
            shadow = candidate;
        }
    }

    if (gap && shadow)
    {
        return shadow->confidence >= gap->confidence ? shadow : gap;
    }
    return gap ? gap : shadow;
}

QByteArray formatNumber(double value)
{
    QByteArray result = QByteArray::number(value, 'f', 6);
    while (result.contains('.') && (result.endsWith('0') || result.endsWith('.')))
    {
        result.chop(1);
    }
    return result.isEmpty() || result == "-" ? QByteArray("0") : result;
}

PDFDictionary copyDictionary(const PDFDocumentBuilder& builder, const PDFObject& object)
{
    if (const PDFDictionary* dictionary = builder.getDictionaryFromObject(object))
    {
        return *dictionary;
    }
    return PDFDictionary();
}

void removeEntry(PDFDictionary& dictionary, const char* key)
{
    if (dictionary.hasKey(key))
    {
        dictionary.removeEntry(key);
    }
}

/// Reads the entries of a number tree (recursively over /Kids)
void readNumberTree(const PDFDocumentBuilder& builder, const PDFObject& node, std::vector<std::pair<PDFInteger, PDFObject>>& entries, int depth)
{
    const PDFDictionary* dictionary = builder.getDictionaryFromObject(node);
    if (!dictionary || depth > 32)
    {
        return;
    }

    const PDFObject& nums = builder.getObject(dictionary->get("Nums"));
    if (nums.isArray())
    {
        const PDFArray* array = nums.getArray();
        for (size_t i = 0; i + 1 < array->getCount(); i += 2)
        {
            const PDFObject& key = builder.getObject(array->getItem(i));
            if (key.isInt())
            {
                entries.emplace_back(key.getInteger(), array->getItem(i + 1));
            }
        }
    }

    const PDFObject& kids = builder.getObject(dictionary->get("Kids"));
    if (kids.isArray())
    {
        for (size_t i = 0; i < kids.getArray()->getCount(); ++i)
        {
            readNumberTree(builder, kids.getArray()->getItem(i), entries, depth + 1);
        }
    }
}

} // namespace

// -------------------------------------------------------------------------
// Analysis
// -------------------------------------------------------------------------

QSizeF PDFScanPreparation::getVisibleSize(const PDFPage* page)
{
    return page ? page->getRotatedCropBox().size() : QSizeF();
}

QTransform PDFScanPreparation::getPageToVisible(const PDFPage* page)
{
    const PageRotation rotation = page->getPageRotation();
    const QRectF rotatedCropBox = page->getRotatedBox(page->getCropBox(), rotation);
    return PDFRenderer::createMediaBoxToDevicePointMatrix(rotatedCropBox, QRectF(QPointF(0, 0), rotatedCropBox.size()), rotation);
}

PDFScanPreparation::PageAnalysis PDFScanPreparation::analyzePage(const PDFOCRPagePreparer& preparer, const PDFDocument* document, PDFInteger pageIndex, const PDFOperationControl* operationControl)
{
    PageAnalysis analysis;
    if (!document || pageIndex < 0 || size_t(pageIndex) >= document->getCatalog()->getPageCount())
    {
        return analysis;
    }

    const PDFPage* page = document->getCatalog()->getPage(pageIndex);
    analysis.pageIndex = pageIndex;
    analysis.visibleSize = getVisibleSize(page);

    // Class of the content and the existing text (INPUT-01)
    const PDFOCRPageAnalysis contentAnalysis = preparer.analyze(pageIndex, operationControl);
    analysis.contentClass = contentAnalysis.contentClass;
    analysis.isScan = contentAnalysis.hasImages && (contentAnalysis.contentClass == PDFOCRPageContentClass::Image ||
                                                   contentAnalysis.contentClass == PDFOCRPageContentClass::Mixed ||
                                                   contentAnalysis.contentClass == PDFOCRPageContentClass::InvisibleText);
    analysis.hasOwnOCRLayer = contentAnalysis.hasOwnOCRLayer;
    analysis.hasText = contentAnalysis.visibleCharacterCount > 0 || (contentAnalysis.invisibleCharacterCount > 0 && !contentAnalysis.hasOwnOCRLayer);
    analysis.hasAnnotations = contentAnalysis.hasAnnotations;
    analysis.notes = contentAnalysis.notes;

    if (const PDFDictionary* pageDictionary = document->getDictionaryFromObject(document->getObjectByReference(page->getPageReference())))
    {
        analysis.isTagged = pageDictionary->hasKey("StructParents");
    }

    // Balance of the content: the deskew encloses the content, a stray "Q" would end it too early
    QByteArray content;
    for (const PDFObjectReference& reference : PDFOCRTextLayerWriter::getPageContentReferences(document, pageIndex))
    {
        const PDFObject& object = document->getObjectByReference(reference);
        if (object.isStream())
        {
            content += document->getDecodedStream(object.getStream());
            content += '\n';
        }
    }
    analysis.hasUnbalancedContent = PDFOCRTextLayerWriter::computeContentBalance(content).hasError;

    const PDFOCRPagePreparer::RasterResult raster = preparer.rasterize(pageIndex, AnalysisDpi, { }, PDFOCRPagePreparer::DefaultMaximumPixels, operationControl);
    if (raster.error || raster.image.isNull())
    {
        analysis.notes << PDFTranslationContext::tr("The page cannot be rendered for the analysis.");
        return analysis;
    }

    analyzeImage(raster.image, raster.geometry.dpi > 0.0 ? raster.geometry.dpi : AnalysisDpi, analysis, operationControl);
    return analysis;
}

void PDFScanPreparation::analyzeImage(const QImage& image, double dpi, PageAnalysis& analysis, const PDFOperationControl* operationControl)
{
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull() || gray.width() < 10 || gray.height() < 10)
    {
        return;
    }

    if (!analysis.visibleSize.isValid())
    {
        analysis.visibleSize = QSizeF(gray.width() / dpi * 72.0, gray.height() / dpi * 72.0);
    }

    // Skew of the lines of the text
    analysis.skewAngle = PDFOCRPagePreparer::estimateSkewAngle(gray, &analysis.skewConfidence, operationControl, MaximumDeskewAngle, true);

    // Ink: the dark pixels, which are not connected with the border
    const int threshold = qMin(computeOtsuThreshold(gray), 200);
    int borderPixels = 0;
    const std::vector<uchar> ink = computeInk(gray, threshold, &borderPixels);

    // Mask of the ink at a low resolution (a cell with at least two ink pixels is ink,
    // so the isolated specks of the scan do not enlarge the content)
    const double scale = MaskDpi / dpi;
    const int maskWidth = qMax(1, qRound(gray.width() * scale));
    const int maskHeight = qMax(1, qRound(gray.height() * scale));
    std::vector<int> cellCounts(size_t(maskWidth) * maskHeight, 0);
    for (int y = 0; y < gray.height(); ++y)
    {
        const int cellY = qMin(maskHeight - 1, int(y * scale));
        for (int x = 0; x < gray.width(); ++x)
        {
            if (ink[size_t(y) * gray.width() + x])
            {
                ++cellCounts[size_t(cellY) * maskWidth + qMin(maskWidth - 1, int(x * scale))];
            }
        }
    }

    analysis.inkMask = QImage(maskWidth, maskHeight, QImage::Format_Mono);
    analysis.inkMask.fill(0);
    int minX = maskWidth;
    int minY = maskHeight;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < maskHeight; ++y)
    {
        for (int x = 0; x < maskWidth; ++x)
        {
            if (cellCounts[size_t(y) * maskWidth + x] >= 2)
            {
                analysis.inkMask.setPixel(x, y, 1);
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }

    if (maxX >= 0)
    {
        const double cellSize = 72.0 / MaskDpi;
        analysis.contentRect = QRectF(minX * cellSize, minY * cellSize, (maxX - minX + 1) * cellSize, (maxY - minY + 1) * cellSize).intersected(QRectF(QPointF(0, 0), analysis.visibleSize));
    }
    else
    {
        analysis.contentRect = QRectF();
    }

    // Gutters of the spreads
    analysis.gutterSideBySide = findGutter(gray, ink, true, dpi);
    analysis.gutterOneAboveAnother = findGutter(gray, ink, false, dpi);
}

std::array<QRectF, 2> PDFScanPreparation::computeSplit(QSizeF visibleSize, SplitOrientation orientation, double position, double gutterWidth)
{
    const double halfGap = qMax(0.0, gutterWidth) * 0.5;
    if (orientation == SplitOrientation::SideBySide)
    {
        const double split = qBound(1.0, position, visibleSize.width() - 1.0);
        const double left = qMax(1.0, split - halfGap);
        const double right = qMin(visibleSize.width() - 1.0, split + halfGap);
        return { QRectF(0, 0, left, visibleSize.height()), QRectF(right, 0, visibleSize.width() - right, visibleSize.height()) };
    }

    const double split = qBound(1.0, position, visibleSize.height() - 1.0);
    const double top = qMax(1.0, split - halfGap);
    const double bottom = qMin(visibleSize.height() - 1.0, split + halfGap);
    return { QRectF(0, 0, visibleSize.width(), top), QRectF(0, bottom, visibleSize.width(), visibleSize.height() - bottom) };
}

QRectF PDFScanPreparation::computeContentCrop(const PageAnalysis& analysis, const QRectF& region, double deskewAngle, double margin)
{
    const QRectF pageRect(QPointF(0, 0), analysis.visibleSize);
    const QRectF area = region.isEmpty() ? pageRect : region.intersected(pageRect);
    if (analysis.inkMask.isNull() || area.isEmpty())
    {
        return area;
    }

    // Ink of the mask inside the region
    const double cellSize = 72.0 / MaskDpi;
    QRectF inkRect;
    const int x0 = qMax(0, int(std::floor(area.left() / cellSize)));
    const int y0 = qMax(0, int(std::floor(area.top() / cellSize)));
    const int x1 = qMin(analysis.inkMask.width() - 1, int(std::ceil(area.right() / cellSize)) - 1);
    const int y1 = qMin(analysis.inkMask.height() - 1, int(std::ceil(area.bottom() / cellSize)) - 1);
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            if (analysis.inkMask.pixelIndex(x, y))
            {
                inkRect = inkRect.united(QRectF(x * cellSize, y * cellSize, cellSize, cellSize));
            }
        }
    }

    if (inkRect.isEmpty())
    {
        return area;
    }

    // The deskew rotates the content around the center of the region
    if (!qFuzzyIsNull(deskewAngle))
    {
        QTransform rotation;
        rotation.translate(area.center().x(), area.center().y());
        rotation.rotate(-deskewAngle);
        rotation.translate(-area.center().x(), -area.center().y());
        inkRect = rotation.mapRect(inkRect);
    }

    return inkRect.adjusted(-margin, -margin, margin, margin).intersected(area);
}

// -------------------------------------------------------------------------
// Plan
// -------------------------------------------------------------------------

PDFScanPreparation::Plan PDFScanPreparation::createIdentityPlan(const PDFDocument* document)
{
    Plan plan;
    if (document)
    {
        for (size_t i = 0; i < document->getCatalog()->getPageCount(); ++i)
        {
            OutputPage page;
            page.sourcePageIndex = PDFInteger(i);
            plan.pages.push_back(page);
        }
    }
    return plan;
}

bool PDFScanPreparation::isSourceChanged(const PDFDocument* document, const Plan& plan, PDFInteger sourcePageIndex)
{
    int count = 0;
    for (const OutputPage& page : plan.pages)
    {
        if (page.sourcePageIndex != sourcePageIndex)
        {
            continue;
        }

        ++count;
        if (!qFuzzyIsNull(page.deskewAngle))
        {
            return true;
        }

        if (!page.visibleRect.isEmpty() && document && sourcePageIndex >= 0 && size_t(sourcePageIndex) < document->getCatalog()->getPageCount())
        {
            const QSizeF visibleSize = getVisibleSize(document->getCatalog()->getPage(sourcePageIndex));
            const QRectF whole(QPointF(0, 0), visibleSize);
            if (std::abs(page.visibleRect.left() - whole.left()) > 0.01 || std::abs(page.visibleRect.top() - whole.top()) > 0.01 ||
                std::abs(page.visibleRect.width() - whole.width()) > 0.01 || std::abs(page.visibleRect.height() - whole.height()) > 0.01)
            {
                return true;
            }
        }
    }
    return count > 1;
}

std::vector<PDFInteger> PDFScanPreparation::getChangedPagesWithOCRLayer(const PDFDocument* document, const Plan& plan)
{
    std::vector<PDFInteger> pages;
    std::set<PDFInteger> sources;
    for (const OutputPage& page : plan.pages)
    {
        sources.insert(page.sourcePageIndex);
    }

    for (PDFInteger source : sources)
    {
        if (source >= 0 && document && size_t(source) < document->getCatalog()->getPageCount() &&
            isSourceChanged(document, plan, source) && PDFOCRTextLayerWriter::readLayerInfo(document, source).isPresent)
        {
            pages.push_back(source);
        }
    }
    return pages;
}

QTransform PDFScanPreparation::getDeskewMatrix(const QRectF& region, double deskewAngle)
{
    // A clockwise skew on the visible page is a clockwise skew in the page space too
    // (the rotation of the page does not mirror it); it is corrected by the rotation
    // counterclockwise, which is the positive angle in the page space (y up)
    const QPointF center = region.center();
    return QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(deskewAngle) * QTransform::fromTranslate(center.x(), center.y());
}

QString PDFScanPreparation::Result::getSummary() const
{
    QStringList parts;
    parts << PDFTranslationContext::tr("%1 page(s) straightened").arg(deskewedPages);
    parts << PDFTranslationContext::tr("%1 page(s) split").arg(splitPages);
    parts << PDFTranslationContext::tr("%1 page(s) cropped").arg(croppedPages);
    QString summary = parts.join(QStringLiteral(", ")) + QChar('.');
    summary += QChar(' ') + PDFTranslationContext::tr("The document has %1 page(s).").arg(pageCount);
    if (!removedLayers.empty())
    {
        summary += QChar(' ') + PDFTranslationContext::tr("OCR layer removed from %1 page(s).").arg(removedLayers.size());
    }
    if (!skippedPages.empty())
    {
        summary += QChar(' ') + PDFTranslationContext::tr("%1 page(s) with an OCR layer were not changed.").arg(skippedPages.size());
    }
    return summary;
}

// -------------------------------------------------------------------------
// Application
// -------------------------------------------------------------------------

PDFScanPreparation::Result PDFScanPreparation::apply(const PDFDocument* document, const Plan& plan, const PDFOperationControl* operationControl)
{
    Result result;
    if (!document)
    {
        result.errorMessage = PDFTranslationContext::tr("No document.");
        return result;
    }

    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());

    // Validation: every page has an output page, the pages keep their order
    std::map<PDFInteger, std::vector<OutputPage>> outputs;
    PDFInteger lastSource = -1;
    for (const OutputPage& page : plan.pages)
    {
        if (page.sourcePageIndex < 0 || page.sourcePageIndex >= pageCount)
        {
            result.errorMessage = PDFTranslationContext::tr("The plan refers to the page %1, which is not a part of the document.").arg(page.sourcePageIndex + 1);
            return result;
        }
        if (page.sourcePageIndex != lastSource)
        {
            if (page.sourcePageIndex < lastSource || outputs.count(page.sourcePageIndex))
            {
                result.errorMessage = PDFTranslationContext::tr("The plan changes the order of the pages; the preparation of the scan keeps the order.");
                return result;
            }
            lastSource = page.sourcePageIndex;
        }
        if (std::abs(page.deskewAngle) > MaximumDeskewAngle + 1e-6)
        {
            result.errorMessage = PDFTranslationContext::tr("The angle of the deskew of the page %1 is out of the range +-%2 degrees.").arg(page.sourcePageIndex + 1).arg(MaximumDeskewAngle);
            return result;
        }
        outputs[page.sourcePageIndex].push_back(page);
    }

    if (PDFInteger(outputs.size()) != pageCount)
    {
        result.errorMessage = PDFTranslationContext::tr("Every page of the document must have at least one output page; the preparation of the scan does not delete pages.");
        return result;
    }

    try
    {
        PDFDocumentBuilder builder(document);

        // Decisions for the pages, which cannot be changed as planned
        for (auto& [source, pages] : outputs)
        {
            if (!isSourceChanged(document, plan, source))
            {
                continue;
            }

            const PDFPage* page = catalog->getPage(source);
            const OutputPage unchanged{ source, QRectF(), 0.0, QRectF() };

            // Own OCR layer: skipped or removed, as the user decided
            if (PDFOCRTextLayerWriter::readLayerInfo(document, source).isPresent)
            {
                auto actionIt = plan.layerActions.find(source);
                const LayerAction action = actionIt != plan.layerActions.end() ? actionIt->second : LayerAction::Skip;
                if (action == LayerAction::Skip)
                {
                    pages = { unchanged };
                    result.skippedPages.push_back(source);
                    continue;
                }

                if (!PDFOCRTextLayerWriter::removeLayer(&builder, document, source))
                {
                    pages = { unchanged };
                    result.skippedPages.push_back(source);
                    result.warnings << PDFTranslationContext::tr("Page %1: the OCR layer cannot be removed (it was changed by another tool), the page is not changed.").arg(source + 1);
                    continue;
                }
                result.removedLayers.push_back(source);
            }

            // A tagged page is a part of the structure tree, its clone would break it
            const PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(page->getPageReference()));
            if (pages.size() > 1 && pageDictionary && pageDictionary->hasKey("StructParents"))
            {
                result.warnings << PDFTranslationContext::tr("Page %1 is a part of the structure tree of a tagged document, it cannot be split.").arg(source + 1);
                OutputPage whole = pages.front();
                whole.visibleRect = QRectF();
                whole.regionRect = QRectF();
                pages = { whole };
            }

            // Deskew of a content with a stray closing operator is refused
            if (std::any_of(pages.begin(), pages.end(), [](const OutputPage& output) { return !qFuzzyIsNull(output.deskewAngle); }))
            {
                QByteArray content;
                for (const PDFObjectReference& reference : PDFOCRTextLayerWriter::getPageContentReferences(document, source))
                {
                    const PDFObject& object = document->getObjectByReference(reference);
                    if (object.isStream())
                    {
                        content += document->getDecodedStream(object.getStream());
                        content += '\n';
                    }
                }
                if (PDFOCRTextLayerWriter::computeContentBalance(content).hasError)
                {
                    result.warnings << PDFTranslationContext::tr("Page %1: the content has a closing operator without its opening operator, the page is not straightened.").arg(source + 1);
                    for (OutputPage& output : pages)
                    {
                        output.deskewAngle = 0.0;
                    }
                }
            }
        }

        const bool changesPageCount = std::any_of(outputs.begin(), outputs.end(), [](const auto& item) { return item.second.size() > 1; });
        if (changesPageCount)
        {
            // Inherited attributes are copied into the pages, so the clones are complete
            builder.flattenPageTree();
        }

        std::vector<PDFObjectReference> newPages;
        std::map<PDFInteger, PDFInteger> firstOutputIndex;
        bool isChanged = !result.removedLayers.empty();

        for (const auto& [source, pages] : outputs)
        {
            if (PDFOperationControl::isOperationCancelled(operationControl))
            {
                result.errorMessage = PDFTranslationContext::tr("Operation was cancelled.");
                return result;
            }

            const PDFPage* sourcePage = catalog->getPage(source);
            const PDFObjectReference sourceReference = sourcePage->getPageReference();
            const QTransform pageToVisible = getPageToVisible(sourcePage);
            const QTransform visibleToPage = pageToVisible.inverted();
            const QRectF mediaBox = sourcePage->getMediaBox();
            const QRectF currentCropBox = sourcePage->getCropBox().isValid() ? sourcePage->getCropBox() : mediaBox;

            firstOutputIndex[source] = PDFInteger(newPages.size());

            // Source page dictionary (after a possible removal of the OCR layer)
            const PDFDictionary sourceDictionary = copyDictionary(builder, builder.getObjectByReference(sourceReference));
            const std::vector<PDFObjectReference> annotations = sourcePage->getAnnotations();

            std::vector<PDFObjectReference> outputReferences;
            std::vector<QRectF> outputCropBoxes;
            for (size_t i = 0; i < pages.size(); ++i)
            {
                const OutputPage& output = pages[i];

                PDFObjectReference reference = sourceReference;
                PDFDictionary dictionary = sourceDictionary;
                if (i > 0)
                {
                    // Clone sharing the content and the resources; the annotations are
                    // distributed below, the thumbnail and the beads belong to the original
                    removeEntry(dictionary, "Annots");
                    removeEntry(dictionary, "Thumb");
                    removeEntry(dictionary, "B");
                    removeEntry(dictionary, "StructParents");
                    reference = builder.addObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(dictionary)));
                }

                // Crop box
                QRectF cropBox = currentCropBox;
                const QRectF whole(QPointF(0, 0), getVisibleSize(sourcePage));
                if (!output.visibleRect.isEmpty() && output.visibleRect != whole)
                {
                    cropBox = visibleToPage.mapRect(output.visibleRect).intersected(mediaBox);
                    if (cropBox.isEmpty())
                    {
                        result.errorMessage = PDFTranslationContext::tr("The crop of the page %1 is outside of the page.").arg(source + 1);
                        return result;
                    }
                }
                const bool isCropped = cropBox != currentCropBox;

                PDFDictionary pageDictionary = copyDictionary(builder, builder.getObjectByReference(reference));
                if (isCropped)
                {
                    PDFObjectFactory boxFactory;
                    boxFactory << cropBox;
                    pageDictionary.setEntry(PDFInplaceOrMemoryString("CropBox"), boxFactory.takeObject());
                    ++result.croppedPages;
                }

                // Deskew: the content of the page is enclosed into the rotation
                if (!qFuzzyIsNull(output.deskewAngle))
                {
                    QByteArray content;
                    std::vector<PDFObjectReference> contentReferences;
                    const PDFObject& contentsObject = pageDictionary.get("Contents");
                    const PDFObject& contents = builder.getObject(contentsObject);
                    if (contentsObject.isReference() && contents.isStream())
                    {
                        contentReferences.push_back(contentsObject.getReference());
                    }
                    else if (contents.isArray())
                    {
                        for (size_t k = 0; k < contents.getArray()->getCount(); ++k)
                        {
                            const PDFObject& item = contents.getArray()->getItem(k);
                            if (item.isReference())
                            {
                                contentReferences.push_back(item.getReference());
                            }
                        }
                    }
                    else if (contents.isStream())
                    {
                        // Direct stream (invalid, but tolerated): it becomes an object
                        contentReferences.push_back(builder.addObject(contents));
                    }

                    for (const PDFObjectReference& contentReference : contentReferences)
                    {
                        const PDFObject& object = builder.getObjectByReference(contentReference);
                        if (object.isStream())
                        {
                            content += builder.getDecodedStream(object.getStream());
                            content += '\n';
                        }
                    }

                    const PDFOCRTextLayerWriter::ContentBalance balance = PDFOCRTextLayerWriter::computeContentBalance(content);
                    const QRectF region = output.regionRect.isEmpty() ? currentCropBox : visibleToPage.mapRect(output.regionRect);
                    const QTransform matrix = getDeskewMatrix(region, output.deskewAngle);

                    // A stray "Q" of the content pops the extra "q" first, the rotation stays
                    QByteArray begin = "q " + formatNumber(matrix.m11()) + ' ' + formatNumber(matrix.m12()) + ' ' + formatNumber(matrix.m21()) + ' ' +
                                       formatNumber(matrix.m22()) + ' ' + formatNumber(matrix.dx()) + ' ' + formatNumber(matrix.dy()) + " cm\n";
                    for (int k = 0; k < qMax(0, -balance.graphicStateDepth); ++k)
                    {
                        begin += "q\n";
                    }

                    QByteArray end;
                    for (int k = 0; k < qMax(0, balance.textObjectDepth); ++k)
                    {
                        end += "ET\n";
                    }
                    for (int k = 0; k < qMax(0, balance.markedContentDepth); ++k)
                    {
                        end += "EMC\n";
                    }
                    for (int k = 0; k < 1 + qMax(0, balance.graphicStateDepth); ++k)
                    {
                        end += "Q\n";
                    }

                    auto createStream = [&builder](const QByteArray& data)
                    {
                        PDFDictionary streamDictionary;
                        streamDictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(data.size()));
                        return builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(streamDictionary), QByteArray(data))));
                    };

                    PDFObjectFactory contentsFactory;
                    contentsFactory.beginArray();
                    contentsFactory << createStream(begin);
                    for (const PDFObjectReference& contentReference : contentReferences)
                    {
                        contentsFactory << contentReference;
                    }
                    contentsFactory << createStream(end);
                    contentsFactory.endArray();
                    pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), contentsFactory.takeObject());
                    ++result.deskewedPages;
                }

                if (isCropped || !qFuzzyIsNull(output.deskewAngle) || i > 0)
                {
                    removeEntry(pageDictionary, "Thumb");
                    builder.setObject(reference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(pageDictionary))));
                    isChanged = true;
                }

                outputReferences.push_back(reference);
                outputCropBoxes.push_back(cropBox);
                newPages.push_back(reference);
            }

            // Annotations of a split page belong to the half containing their center
            if (outputReferences.size() > 1)
            {
                ++result.splitPages;
                std::vector<std::vector<PDFObjectReference>> distributed(outputReferences.size());
                for (const PDFObjectReference& annotation : annotations)
                {
                    size_t target = 0;
                    if (const PDFDictionary* annotationDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(annotation)))
                    {
                        PDFDocumentDataLoaderDecorator loader(builder.getStorage());
                        const QRectF rect = loader.readRectangle(annotationDictionary->get("Rect"), QRectF());
                        for (size_t k = 0; k < outputCropBoxes.size(); ++k)
                        {
                            if (outputCropBoxes[k].contains(rect.center()))
                            {
                                target = k;
                                break;
                            }
                        }
                    }
                    distributed[target].push_back(annotation);

                    PDFObjectFactory parentFactory;
                    parentFactory.beginDictionary();
                    parentFactory.beginDictionaryItem("P");
                    parentFactory << outputReferences[target];
                    parentFactory.endDictionaryItem();
                    parentFactory.endDictionary();
                    builder.mergeTo(annotation, parentFactory.takeObject());
                }

                for (size_t k = 0; k < outputReferences.size(); ++k)
                {
                    PDFDictionary pageDictionary = copyDictionary(builder, builder.getObjectByReference(outputReferences[k]));
                    removeEntry(pageDictionary, "Annots");
                    if (!distributed[k].empty())
                    {
                        PDFObjectFactory annotationsFactory;
                        annotationsFactory << distributed[k];
                        pageDictionary.setEntry(PDFInplaceOrMemoryString("Annots"), annotationsFactory.takeObject());
                    }
                    builder.setObject(outputReferences[k], PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(pageDictionary))));
                }
            }
        }

        result.pageCount = int(newPages.size());
        if (!isChanged)
        {
            // Nothing is changed (identity plan, or all changed pages were skipped)
            return result;
        }

        if (changesPageCount)
        {
            builder.setPages(newPages);

            // Page labels are keyed by the page index: the indices behind the inserted pages
            // are shifted, the labels of the spreads continue into the added pages
            if (const PDFDictionary* catalogDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference())))
            {
                if (catalogDictionary->hasKey("PageLabels"))
                {
                    std::vector<std::pair<PDFInteger, PDFObject>> entries;
                    readNumberTree(builder, catalogDictionary->get("PageLabels"), entries, 0);
                    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) { return left.first < right.first; });

                    PDFObjectFactory labelsFactory;
                    labelsFactory.beginDictionary();
                    labelsFactory.beginDictionaryItem("Nums");
                    labelsFactory.beginArray();
                    for (const auto& [index, label] : entries)
                    {
                        auto it = firstOutputIndex.find(index);
                        if (it == firstOutputIndex.end())
                        {
                            continue;
                        }
                        labelsFactory << it->second;
                        labelsFactory << label;
                    }
                    labelsFactory.endArray();
                    labelsFactory.endDictionaryItem();
                    labelsFactory.endDictionary();

                    PDFDictionary newCatalog = *catalogDictionary;
                    newCatalog.setEntry(PDFInplaceOrMemoryString("PageLabels"), labelsFactory.takeObject());
                    builder.setObject(builder.getCatalogReference(), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(newCatalog))));
                }
            }
        }

        if (result.deskewedPages > 0 && std::any_of(outputs.begin(), outputs.end(), [&](const auto& item)
        {
            return std::any_of(item.second.begin(), item.second.end(), [](const OutputPage& output) { return !qFuzzyIsNull(output.deskewAngle); }) &&
                   !catalog->getPage(item.first)->getAnnotations().empty();
        }))
        {
            result.warnings << PDFTranslationContext::tr("Annotations of the straightened pages are not rotated.");
        }

        result.document = PDFDocumentPointer(new PDFDocument(builder.build()));
    }
    catch (const PDFException& exception)
    {
        result.errorMessage = exception.getMessage();
        result.document.reset();
    }
    catch (const std::exception& exception)
    {
        result.errorMessage = QString::fromLocal8Bit(exception.what());
        result.document.reset();
    }

    return result;
}

}   // namespace pdf
