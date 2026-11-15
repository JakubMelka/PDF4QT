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

#include "pdfdocumenttool_pdftoimages.h"
#include "pdfdocumenttool_split.h"

#include "pdfcatalog.h"
#include "pdfconstants.h"
#include "pdffont.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoperationcontrol.h"
#include "pdfpage.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QLocale>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

#include <atomic>
#include <cmath>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the exports of images
class PDFDocumentToolPdfToImagesHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolPdfToImages)

public:
    PDFDocumentToolPdfToImagesHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// The cancellation of the run or a stop of the file sink (Stop Export of the question): the
    /// pool sees both, the run reads whether it was stopped. Thread-safe.
    class StoppableControl : public PDFOperationControl
    {
    public:
        explicit StoppableControl(const PDFOperationControl* control) : m_control(control) { }

        virtual bool isOperationCanceled() const override
        {
            return m_isStopped.load(std::memory_order_acquire) || PDFOperationControl::isOperationCanceled(m_control);
        }

        void stop() { m_isStopped.store(true, std::memory_order_release); }

    private:
        const PDFOperationControl* m_control;
        std::atomic<bool> m_isStopped = false;
    };

    /// "Pages 412 to 1,592", "Page 3", "5 pages" (356:12504)
    static QString formatPages(const std::vector<PDFInteger>& pages)
    {
        if (pages.empty())
        {
            return QString();
        }
        if (pages.size() == 1)
        {
            return tr("Page %1").arg(QLocale().toString(pages.front() + 1));
        }

        bool isContiguous = true;
        for (size_t i = 1; i < pages.size() && isContiguous; ++i)
        {
            isContiguous = pages[i] == pages[i - 1] + 1;
        }
        if (isContiguous)
        {
            return tr("Pages %1 to %2").arg(QLocale().toString(pages.front() + 1), QLocale().toString(pages.back() + 1));
        }
        return tr("%Ln pages", nullptr, int(qMin<size_t>(pages.size(), size_t(std::numeric_limits<int>::max()))));
    }

    /// The text of a side of a page too large ("Page 12 would be 70,157 px high – lower the resolution")
    static QString formatTooLarge(PDFInteger pageIndex, int pixels, bool isHeight)
    {
        const QString page = QLocale().toString(pageIndex + 1);
        const QString value = QLocale().toString(pixels);
        return isHeight ? tr("Page %1 would be %2 px high – lower the resolution").arg(page, value)
                        : tr("Page %1 would be %2 px wide – lower the resolution").arg(page, value);
    }
};

// ------------------------------------------------------- PDFDocumentToolImageFormat

PDFDocumentToolImageFormat PDFDocumentToolImageFormat::fromParameters(const QVariantMap& parameters)
{
    PDFDocumentToolImageFormat format;
    const QByteArray formatName = parameters.value(QString::fromLatin1(PARAMETER_FORMAT)).toString().trimmed().toLower().toLatin1();
    if (!formatName.isEmpty())
    {
        format.format = formatName;
    }
    format.subtype = parameters.value(QString::fromLatin1(PARAMETER_SUBTYPE)).toString().toLatin1();
    format.compression = parameters.value(QString::fromLatin1(PARAMETER_COMPRESSION), -1).toInt();
    format.quality = parameters.value(QString::fromLatin1(PARAMETER_QUALITY), -1).toInt();
    format.optimizedWrite = parameters.value(QString::fromLatin1(PARAMETER_OPTIMIZED_WRITE)).toBool();
    format.progressiveScanWrite = parameters.value(QString::fromLatin1(PARAMETER_PROGRESSIVE_SCAN_WRITE)).toBool();
    return format;
}

PDFDocumentToolImageFormat PDFDocumentToolImageFormat::fromWriterSettings(const PDFImageWriterSettings& settings)
{
    PDFDocumentToolImageFormat format;
    format.format = settings.getCurrentFormat().toLower();
    format.subtype = settings.getCurrentSubtype();
    format.compression = settings.isOptionSupported(QImageIOHandler::CompressionRatio) ? settings.getCompression() : -1;
    format.quality = settings.isOptionSupported(QImageIOHandler::Quality) ? settings.getQuality() : -1;
    format.optimizedWrite = settings.isOptionSupported(QImageIOHandler::OptimizedWrite) && settings.hasOptimizedWrite();
    format.progressiveScanWrite = settings.isOptionSupported(QImageIOHandler::ProgressiveScanWrite) && settings.hasProgressiveScanWrite();
    return format;
}

QVariantMap PDFDocumentToolImageFormat::toParameters() const
{
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PARAMETER_FORMAT), QString::fromLatin1(format));
    parameters.insert(QString::fromLatin1(PARAMETER_SUBTYPE), QString::fromLatin1(subtype));
    parameters.insert(QString::fromLatin1(PARAMETER_COMPRESSION), compression);
    parameters.insert(QString::fromLatin1(PARAMETER_QUALITY), quality);
    parameters.insert(QString::fromLatin1(PARAMETER_OPTIMIZED_WRITE), optimizedWrite);
    parameters.insert(QString::fromLatin1(PARAMETER_PROGRESSIVE_SCAN_WRITE), progressiveScanWrite);
    return parameters;
}

QList<QByteArray> PDFDocumentToolImageFormat::getSupportedFormats()
{
    std::set<QByteArray> formats;
    for (const QByteArray& format : QImageWriter::supportedImageFormats())
    {
        formats.insert(format.toLower());
    }
    return QList<QByteArray>(formats.cbegin(), formats.cend());
}

bool PDFDocumentToolImageFormat::isValid(QString* errorMessage) const
{
    const QList<QByteArray> formats = getSupportedFormats();
    if (formats.contains(format))
    {
        return true;
    }

    QStringList names;
    for (const QByteArray& name : formats)
    {
        names << QString::fromLatin1(name);
    }
    *errorMessage = tr("Images cannot be written in the format \"%1\". Use one of: %2.").arg(QString::fromLatin1(format), names.join(QStringLiteral(", ")));
    return false;
}

QString PDFDocumentToolImageFormat::getExtension() const
{
    if (format == "jpeg")
    {
        return QStringLiteral(".jpg");
    }
    return QLatin1Char('.') + QString::fromLatin1(format);
}

QString PDFDocumentToolImageFormat::getDisplayName() const
{
    return QString::fromLatin1(format == "jpg" ? QByteArray("jpeg") : format).toUpper();
}

int PDFDocumentToolImageFormat::getMaximumSide() const
{
    if (format == "webp")
    {
        return MAXIMUM_WEBP_SIDE;
    }
    if (format == "jpeg" || format == "jpg")
    {
        return MAXIMUM_JPEG_SIDE;
    }
    return MAXIMUM_SIDE;
}

bool PDFDocumentToolImageFormat::hasAlpha() const
{
    static const std::set<QByteArray> withoutAlpha = { "jpeg", "jpg", "bmp", "ppm", "pgm", "pbm", "xbm", "wbmp" };
    return withoutAlpha.count(format) == 0;
}

QByteArray PDFDocumentToolImageFormat::encode(const QImage& image, QString* errorMessage) const
{
    if (image.isNull())
    {
        *errorMessage = tr("The image is empty.");
        return QByteArray();
    }

    // A format without alpha gets the image over a white background (as the page is drawn)
    QImage written = image;
    if (!hasAlpha() && image.hasAlphaChannel())
    {
        written = QImage(image.size(), QImage::Format_RGB32);
        if (written.isNull())
        {
            *errorMessage = tr("There is not enough memory for the image.");
            return QByteArray();
        }
        written.fill(Qt::white);
        QPainter painter(&written);
        painter.drawImage(0, 0, image);
    }

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    if (!subtype.isEmpty())
    {
        writer.setSubType(subtype);
    }
    if (compression >= 0)
    {
        writer.setCompression(compression);
    }
    if (quality >= 0)
    {
        writer.setQuality(quality);
    }
    writer.setOptimizedWrite(optimizedWrite);
    writer.setProgressiveScanWrite(progressiveScanWrite);

    if (!writer.write(written))
    {
        *errorMessage = writer.errorString();
        return QByteArray();
    }
    return data;
}

// ------------------------------------------------------- PDFDocumentToolPageRenderSettings

PDFRenderer::Features PDFDocumentToolPageRenderSettings::getOutputFeatures(PDFRenderer::Features features)
{
    constexpr PDFRenderer::Features removed = PDFRenderer::Features(PDFRenderer::DisplayTimes | PDFRenderer::DebugTextBlocks | PDFRenderer::DebugTextLines | PDFRenderer::LogicalSizeZooming);
    features &= ~removed;
    features |= PDFRenderer::ClipToCropBox;
    return features;
}

PDFRenderer::Features PDFDocumentToolPageRenderSettings::getExportFeatures(PDFRenderer::Features features)
{
    constexpr PDFRenderer::Features removed = PDFRenderer::Features(PDFRenderer::IgnoreOptionalContent |
                                                                    PDFRenderer::ColorAdjust_Invert | PDFRenderer::ColorAdjust_Grayscale | PDFRenderer::ColorAdjust_HighContrast |
                                                                    PDFRenderer::ColorAdjust_Bitonal | PDFRenderer::ColorAdjust_CustomColors | PDFRenderer::ColorAdjust_Annotations);
    return getOutputFeatures(features & ~removed);
}

PDFDocumentToolPageRenderSettings PDFDocumentToolPageRenderSettings::create(const PDFDocumentToolRenderContext& rendering)
{
    PDFDocumentToolPageRenderSettings settings;
    settings.cmsSettings = rendering.cmsSettings;
    if (rendering.features)
    {
        // The settings of a view: its color modes are no content (R4-10 b)
        settings.features = getExportFeatures(*rendering.features);
    }
    if (rendering.engine)
    {
        settings.engine = *rendering.engine;
    }
    settings.layerStates = rendering.layerStates;
    return settings;
}

// ------------------------------------------------------- PDFDocumentToolPdfToImages

QString PDFDocumentToolPdfToImages::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolPdfToImages::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages, which are rendered")).setStored(false));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_FORMAT), QT_TR_NOOP("Format"),
                                                   QT_TR_NOOP("Image format: png, jpeg, tiff, webp, bmp or another format of the image writer"), QString::fromLatin1(DEFAULT_FORMAT), 32));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_SUBTYPE), QT_TR_NOOP("Subtype"),
                                                   QT_TR_NOOP("Subtype of the format, if it has any; empty = the default"), QString(), 64));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_COMPRESSION), QT_TR_NOOP("Compression"),
                                                    QT_TR_NOOP("Compression level of the format; -1 = the default of the format"), -1, -1, 100));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_QUALITY), QT_TR_NOOP("Quality"),
                                                    QT_TR_NOOP("Quality of a lossy format, 0 to 100; -1 = the default of the format"), -1, -1, 100));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_OPTIMIZED_WRITE), QT_TR_NOOP("Optimized write"),
                                                 QT_TR_NOOP("Optimize the written file (JPEG only)"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_PROGRESSIVE_SCAN_WRITE), QT_TR_NOOP("Progressive scan write"),
                                                 QT_TR_NOOP("Write a progressive image (JPEG only)"), false));
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_SIZE_BY), QT_TR_NOOP("Size by"), QT_TR_NOOP("How the size of the images is given"),
                                                 {
                                                     { QString::fromLatin1(SIZE_BY_DPI), QT_TR_NOOP("Resolution"), QT_TR_NOOP("72 to 6000 dpi") },
                                                     { QString::fromLatin1(SIZE_BY_PIXELS), QT_TR_NOOP("Pixels"), QT_TR_NOOP("Longer side from 100 to 16,384 px") },
                                                 },
                                                 QString::fromLatin1(SIZE_BY_DPI)));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_DPI), QT_TR_NOOP("Resolution"),
                                                    QT_TR_NOOP("Dots per inch of the rendered pages"), DEFAULT_DPI, MINIMUM_DPI, MAXIMUM_DPI));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_PIXELS), QT_TR_NOOP("Longer side"),
                                                    QT_TR_NOOP("Pixels of the longer side of each image"), DEFAULT_PIXELS, MINIMUM_PIXELS, MAXIMUM_PIXELS));
    PDFDocumentToolFileOutput::addDefinitions(&definitions, tr, QString::fromLatin1(DEFAULT_FILE_NAME));
    PDFDocumentToolFileOutput::addOnlyOutputsDefinition(&definitions, tr);
    return definitions;
}

PDFDocumentToolPdfToImages::PageGeometry PDFDocumentToolPdfToImages::getPageGeometry(const PDFPage* page, const QVariantMap& parameters)
{
    using Helper = PDFDocumentToolPdfToImagesHelper;

    PageGeometry geometry;
    if (!page)
    {
        return geometry;
    }

    // B57: the visible area of the page is the crop box (clipped by the media box, 14.11.2),
    // turned by /Rotate; /UserUnit is the size of a unit of the default user space in 1/72 inch
    const QRectF cropBox = page->getRotatedEffectiveCropBox();
    const QRectF mediaBox = page->getRotatedMediaBox();
    if (cropBox.isEmpty() || mediaBox.isEmpty())
    {
        return geometry;
    }

    double scale = 0.0;
    if (parameters.value(Helper::name(PARAMETER_SIZE_BY)).toString() == QLatin1String(SIZE_BY_PIXELS))
    {
        const int pixels = parameters.value(Helper::name(PARAMETER_PIXELS), DEFAULT_PIXELS).toInt();
        scale = double(pixels) / qMax(cropBox.width(), cropBox.height());
    }
    else
    {
        const int dpi = parameters.value(Helper::name(PARAMETER_DPI), DEFAULT_DPI).toInt();
        scale = double(dpi) * PDF_POINT_TO_INCH * page->getUserUnit();
    }

    if (!std::isfinite(scale) || scale <= 0.0)
    {
        return geometry;
    }

    // The sizes are bounded before they become integers: a hostile page of a huge box
    const auto toPixels = [](double length) { return int(qBound(1.0, std::round(length), double(std::numeric_limits<int>::max() / 2))); };
    geometry.imageSize = QSize(toPixels(cropBox.width() * scale), toPixels(cropBox.height() * scale));
    geometry.renderSize = QSize(toPixels(mediaBox.width() * scale), toPixels(mediaBox.height() * scale));

    if (geometry.imageSize == geometry.renderSize)
    {
        geometry.cropRect = QRect(QPoint(0, 0), geometry.renderSize);
        return geometry;
    }

    // The renderer maps the rotated media box to the image; the crop box is a part of it
    const QTransform matrix = PDFRenderer::createPagePointToDevicePointMatrix(page, QRectF(QPointF(0, 0), QSizeF(geometry.renderSize)), PageRotation::None);
    const QRectF cropDevice = matrix.mapRect(page->getEffectiveCropBox());
    QRect cropRect(QPoint(int(std::lround(cropDevice.left())), int(std::lround(cropDevice.top()))), geometry.imageSize);
    cropRect.moveLeft(qBound(0, cropRect.left(), qMax(0, geometry.renderSize.width() - cropRect.width())));
    cropRect.moveTop(qBound(0, cropRect.top(), qMax(0, geometry.renderSize.height() - cropRect.height())));
    geometry.cropRect = cropRect.intersected(QRect(QPoint(0, 0), geometry.renderSize));
    geometry.imageSize = geometry.cropRect.size();
    return geometry;
}

QString PDFDocumentToolPdfToImages::checkPageGeometry(PDFInteger pageIndex, const PageGeometry& geometry, const PDFDocumentToolImageFormat& format)
{
    using Helper = PDFDocumentToolPdfToImagesHelper;

    if (!geometry.isValid())
    {
        return tr("Page %1 has no area to render.").arg(QLocale().toString(pageIndex + 1));
    }

    // The written image by the limit of its format, the rendered one by the raster engine and 2 GiB
    const int maximumSide = qMin(format.getMaximumSide(), PDFDocumentToolImageFormat::MAXIMUM_SIDE);
    const QSize largest(qMax(geometry.imageSize.width(), geometry.renderSize.width()), qMax(geometry.imageSize.height(), geometry.renderSize.height()));
    if (largest.height() > maximumSide)
    {
        return Helper::formatTooLarge(pageIndex, largest.height(), true);
    }
    if (largest.width() > maximumSide)
    {
        return Helper::formatTooLarge(pageIndex, largest.width(), false);
    }
    if (PDFRasterizerPool::getImageDataSize(geometry.renderSize) > PDFDocumentToolImageFormat::MAXIMUM_IMAGE_BYTES)
    {
        return tr("Page %1 would need more than 2 GB of memory – lower the resolution").arg(QLocale().toString(pageIndex + 1));
    }
    return QString();
}

std::vector<PDFInteger> PDFDocumentToolPdfToImages::getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    using Helper = PDFDocumentToolPdfToImagesHelper;

    const QVariant value = parameters.value(Helper::name(PARAMETER_PAGES));
    const std::optional<PDFDocumentToolPageSelection> selection = value.isValid() ? PDFDocumentToolPageSelection::fromVariant(value, errorMessage)
                                                                                 : std::optional<PDFDocumentToolPageSelection>(PDFDocumentToolPageSelection());
    if (!selection)
    {
        return {};
    }
    std::vector<PDFInteger> pages = selection->getPageIndices(document, errorMessage);
    if (pages.empty() && errorMessage->isEmpty())
    {
        *errorMessage = tr("No page is selected.");
    }
    return pages;
}

bool PDFDocumentToolPdfToImages::createFileNames(const std::vector<PDFInteger>& pages, const QString& sourceFileName, const QVariantMap& parameters, const QDate& date, QStringList* fileNames, QString* errorMessage)
{
    using Helper = PDFDocumentToolPdfToImagesHelper;

    // 339:5080: the name of each image differs by its page
    const QString pattern = parameters.value(Helper::name(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME)).toString();
    if (!pattern.contains(QLatin1Char('@')) && !pattern.contains(QLatin1String("{source_page}")))
    {
        *errorMessage = tr("The name must contain @ for the page number");
        return false;
    }

    std::vector<PDFDocumentToolFileOutput::File> files(pages.size());
    for (size_t i = 0; i < pages.size(); ++i)
    {
        files[i].pages = { pages[i] };
    }

    const PDFFileNameTemplate nameTemplate(pattern, PDFDocumentToolImageFormat::fromParameters(parameters).getExtension());
    if (!PDFDocumentToolFileOutput::createFileNames(&files, nameTemplate, sourceFileName, date, errorMessage))
    {
        return false;
    }

    fileNames->clear();
    for (const PDFDocumentToolFileOutput::File& file : files)
    {
        *fileNames << file.fileName;
    }
    return true;
}

QDate PDFDocumentToolPdfToImages::getRunDate(const QVariantMap& parameters, const std::function<QStringList(const QDate&)>& createNames)
{
    const QDate today = QDate::currentDate();
    const QStringList onlyOutputs = PDFDocumentToolFileOutput::getOnlyOutputs(parameters);
    if (onlyOutputs.isEmpty())
    {
        return today;
    }

    // Today first (a template without {date}, a Retry of the same day), then the dates of the
    // names, each once; a hostile list of names is bounded by the number of the tried dates
    constexpr size_t maximumDates = 16;
    static const QRegularExpression dateExpression(QStringLiteral("\\d{4}-\\d{2}-\\d{2}"));
    std::vector<QDate> dates{ today };
    for (const QString& name : onlyOutputs)
    {
        QRegularExpressionMatchIterator it = dateExpression.globalMatch(name);
        while (it.hasNext() && dates.size() < maximumDates)
        {
            const QDate date = QDate::fromString(it.next().captured(), Qt::ISODate);
            if (date.isValid() && std::find(dates.cbegin(), dates.cend(), date) == dates.cend())
            {
                dates.push_back(date);
            }
        }
    }

    for (const QDate& date : dates)
    {
        const QStringList names = createNames(date);
        const QSet<QString> nameSet(names.cbegin(), names.cend());
        if (std::all_of(onlyOutputs.cbegin(), onlyOutputs.cend(), [&nameSet](const QString& name) { return nameSet.contains(name); }))
        {
            return date;
        }
    }
    return today;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolPdfToImages::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolPdfToImagesHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (!errors.empty() || !document)
    {
        return errors;
    }

    QString errorMessage;
    const std::vector<PDFInteger> pages = getPages(document, parameters, &errorMessage);
    if (pages.empty())
    {
        errors.push_back({ Helper::name(PARAMETER_PAGES), errorMessage });
        return errors;
    }

    const PDFDocumentToolImageFormat format = PDFDocumentToolImageFormat::fromParameters(parameters);
    if (!format.isValid(&errorMessage))
    {
        errors.push_back({ Helper::name(PARAMETER_FORMAT), errorMessage });
        return errors;
    }

    QStringList fileNames;
    if (!createFileNames(pages, context.documents.front().fileName, parameters, QDate(), &fileNames, &errorMessage))
    {
        errors.push_back({ Helper::name(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), errorMessage });
    }

    // R4-19: the first page, which is too large for the format, is the error of the resolution
    const QString sizeParameter = parameters.value(Helper::name(PARAMETER_SIZE_BY)).toString() == QLatin1String(SIZE_BY_PIXELS) ? Helper::name(PARAMETER_PIXELS) : Helper::name(PARAMETER_DPI);
    for (const PDFInteger pageIndex : pages)
    {
        const QString pageError = checkPageGeometry(pageIndex, getPageGeometry(document->getCatalog()->getPage(pageIndex), parameters), format);
        if (!pageError.isEmpty())
        {
            errors.push_back({ sizeParameter, pageError });
            break;
        }
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolPdfToImages::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    // R4-10 b: the color management, the renderer and the layers of the context (the settings and
    // the tab of the application; empty = OCUsage::Export and the defaults)
    return exportPages(context, parameters, PDFDocumentToolPageRenderSettings::create(context.rendering), control, progress);
}

PDFDocumentToolResult PDFDocumentToolPdfToImages::exportPages(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFDocumentToolPageRenderSettings& renderSettings,
                                                             const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolPdfToImagesHelper;

    QElapsedTimer timer;
    timer.start();

    PDFDocumentToolFileSink* sink = context.fileSink;
    if (!sink)
    {
        return PDFDocumentToolResult::createFailed(tr("The files have no place to be written."));
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    QString errorMessage;
    const std::vector<PDFInteger> allPages = getPages(document, parameters, &errorMessage);
    if (allPages.empty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    // Retry N Files writes the files of the earlier run: the date of {date} is the date of that run
    const QDate runDate = getRunDate(parameters, [&](const QDate& date)
    {
        QStringList names;
        QString ignoredError;
        createFileNames(allPages, input.fileName, parameters, date, &names, &ignoredError);
        return names;
    });
    QStringList allNames;
    if (!createFileNames(allPages, input.fileName, parameters, runDate, &allNames, &errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    const PDFDocumentToolImageFormat format = PDFDocumentToolImageFormat::fromParameters(parameters);
    if (!format.isValid(&errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    // Retry N Files (K4.11): only the names of the earlier run, which failed
    const QStringList onlyOutputs = PDFDocumentToolFileOutput::getOnlyOutputs(parameters);
    std::vector<PDFInteger> pages;
    QStringList fileNames;
    std::map<PDFInteger, QString> nameOfPage;
    for (size_t i = 0; i < allPages.size(); ++i)
    {
        if (onlyOutputs.isEmpty() || onlyOutputs.contains(allNames[qsizetype(i)]))
        {
            pages.push_back(allPages[i]);
            fileNames << allNames[qsizetype(i)];
            nameOfPage[allPages[i]] = allNames[qsizetype(i)];
        }
    }
    if (pages.empty())
    {
        return PDFDocumentToolResult::createFailed(tr("None of the files to write again belongs to this export."));
    }

    // A page too large for the format would fail in the middle of the run (R4-19)
    for (const PDFInteger pageIndex : pages)
    {
        const QString pageError = checkPageGeometry(pageIndex, getPageGeometry(document->getCatalog()->getPage(pageIndex), parameters), format);
        if (!pageError.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(pageError);
        }
    }

    if (!sink->begin(fileNames, &errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    // The objects of the rendering belong to this run and to its thread; the pool and its
    // rasterizers die at the end of this function, after the last thread of the pool has ended
    PDFOptionalContentActivity optionalContentActivity(document, renderSettings.layerStates ? OCUsage::View : OCUsage::Export, nullptr);
    if (renderSettings.layerStates)
    {
        for (const auto& [ocg, state] : *renderSettings.layerStates)
        {
            optionalContentActivity.setState(ocg, state, false);
        }
    }

    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(document);
    cmsManager.setSettings(renderSettings.cmsSettings ? *renderSettings.cmsSettings : cmsManager.getDefaultSettings());

    PDFMeshQualitySettings meshQualitySettings;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    const PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    const int rasterizerCount = PDFRasterizerPool::getCorrectedRasterizerCount(renderSettings.rasterizerCount > 0 ? renderSettings.rasterizerCount : PDFRasterizerPool::getDefaultRasterizerCount());
    // The features as given (the explicit options of PdfTool render; a run of the application has
    // them by create), never the debugging ones
    PDFRasterizerPool pool(document, &fontCache, &iccProfileCache, &cmsManager, &optionalContentActivity, PDFDocumentToolPageRenderSettings::getOutputFeatures(renderSettings.features),
                           meshQualitySettings, rasterizerCount, renderSettings.engine, nullptr);

    Helper::StoppableControl stoppableControl(control);

    const PDFRasterizerPool::PageImageSizeGetter getImageSize = [&parameters](const PDFPage* page)
    {
        return getPageGeometry(page, parameters).renderSize;
    };

    // The encoder runs in the threads of the pool, more pages at once: it reads only the
    // immutable document and the parameters
    const PDFRasterizerPool::EncodeImageMethod encode = [&](const PDFRenderedPageImage& image, QString* encodeError) -> QByteArray
    {
        if (renderSettings.pageObserver)
        {
            renderSettings.pageObserver(image);
        }

        const PageGeometry geometry = getPageGeometry(document->getCatalog()->getPage(image.pageIndex), parameters);
        if (geometry.cropRect == image.pageImage.rect())
        {
            return format.encode(image.pageImage, encodeError);
        }
        return format.encode(image.pageImage.copy(geometry.cropRect), encodeError);
    };

    // The writer runs one call at a time in the order of the pages (renderOrdered): the sink is
    // used by one thread at a time, each call happens after the previous one
    qint64 sizeTotal = 0;
    std::vector<PDFDocumentToolFileProblem> encoderFailures;
    std::vector<PDFDocumentToolPageProblem> renderingIssues;
    const PDFRasterizerPool::WriteOutputMethod write = [&](PDFRenderedPageOutput& output)
    {
        const QString& fileName = nameOfPage[output.pageIndex];
        if (!output.isRendered)
        {
            encoderFailures.push_back({ fileName, output.errorMessage });
            return;
        }

        // 339:6505: the problems of the page, each text once
        QStringList messages;
        for (const PDFRenderError& error : output.errors)
        {
            if (error.type != RenderErrorType::Information && !messages.contains(error.message))
            {
                messages << error.message;
                renderingIssues.push_back({ output.pageIndex, error.message });
            }
        }

        QString writeError;
        if (sink->write(fileName, output.data, &writeError))
        {
            sizeTotal += output.data.size();
        }
        if (sink->isStopped())
        {
            stoppableControl.stop();
        }
    };

    pool.renderOrdered(pages, getImageSize, encode, write, progress, &stoppableControl);
    fontCache.setCacheShrinkEnabled(nullptr, true);

    const bool isStopped = stoppableControl.isOperationCanceled();

    PDFDocumentToolResult result;
    result.status = isStopped ? PDFDocumentToolResultStatus::Stopped : PDFDocumentToolResultStatus::Success;
    result.writtenFiles = sink->getWrittenFiles();
    result.notWrittenFiles = sink->getNotWrittenFiles();
    for (const auto& [fileName, reason] : sink->getFailedFiles())
    {
        result.failedFiles.push_back({ fileName, reason });
    }
    for (const PDFDocumentToolFileProblem& failure : encoderFailures)
    {
        result.failedFiles.push_back(failure);
        result.notWrittenFiles << failure.fileName;
    }
    result.renderingIssues = std::move(renderingIssues);

    // The files after a stop are not written either (in their order); sets, a run has up to
    // 10 000 files. A renamed file (Rename) is written under another name: its page is exported.
    QStringList writtenNames;
    for (const QString& path : result.writtenFiles)
    {
        writtenNames << QFileInfo(path).fileName();
    }
    QSet<QString> notWritten(result.notWrittenFiles.cbegin(), result.notWrittenFiles.cend());
    const int writtenCount = int(result.writtenFiles.size());

    // The writer gets the pages in their order: the first files were handed to it, the rest not
    const size_t handledCount = size_t(writtenCount) + size_t(notWritten.size());
    std::vector<PDFInteger> notExportedPages;
    for (size_t i = 0; i < pages.size(); ++i)
    {
        const QString& fileName = fileNames[qsizetype(i)];
        if (i >= handledCount && !notWritten.contains(fileName))
        {
            notWritten.insert(fileName);
            result.notWrittenFiles << fileName;
        }
        if (notWritten.contains(fileName))
        {
            notExportedPages.push_back(pages[i]);
        }
    }

    const qint64 duration = timer.elapsed();
    const QString formatName = format.getDisplayName();
    const QString sizeText = PDFDocumentToolFormat::formatSize(sizeTotal);
    if (isStopped)
    {
        // 356:12504: Written "411 PNG · 540 MB", Not exported "Pages 412 to 1,592"
        result.facts.push_back({ QStringLiteral("written"), tr("Written"), tr("%Ln %1 · %2", nullptr, writtenCount).arg(formatName, sizeText) });
        result.facts.push_back({ QStringLiteral("notExported"), tr("Not exported"), Helper::formatPages(notExportedPages) });
    }
    else if (!result.failedFiles.empty())
    {
        // 339:7293: Written "1,589 PNG · 2.1 GB", Failed "3 files"
        result.facts.push_back({ QStringLiteral("written"), tr("Written"), tr("%Ln %1 · %2", nullptr, writtenCount).arg(formatName, sizeText) });
        result.facts.push_back({ QStringLiteral("failed"), tr("Failed"), tr("%Ln files", nullptr, int(result.failedFiles.size())) });
    }
    else
    {
        // 339:6505: Files "1,592 PNG · Report_1.png … Report_1592.png", Total size "2.1 GB"
        result.facts.push_back({ QStringLiteral("files"), tr("Files"), tr("%Ln %1 · %2", nullptr, writtenCount).arg(formatName, PDFDocumentToolFileOutput::formatFileNames(writtenNames)) });
        result.facts.push_back({ QStringLiteral("size"), tr("Total size"), sizeText });
        if (!result.notWrittenFiles.isEmpty())
        {
            // Skipped by the policy of the existing files
            result.facts.push_back({ QStringLiteral("notWritten"), tr("Not written"), tr("%Ln · %1", nullptr, int(result.notWrittenFiles.size())).arg(PDFDocumentToolFileOutput::formatFileNames(result.notWrittenFiles)) });
        }
    }
    result.facts.push_back({ QStringLiteral("time"), tr("Time"), PDFDocumentToolFormat::formatDuration(duration) });

    if (!result.renderingIssues.empty())
    {
        std::set<PDFInteger> issuePages;
        for (const PDFDocumentToolPageProblem& problem : result.renderingIssues)
        {
            issuePages.insert(problem.pageIndex);
        }
        result.facts.push_back({ QStringLiteral("renderingIssues"), tr("Rendering issues"), tr("%Ln pages", nullptr, int(issuePages.size())) });
    }

    result.data.insert(QStringLiteral("fileCount"), qint64(fileNames.size()));
    result.data.insert(QStringLiteral("writtenCount"), qint64(writtenCount));
    result.data.insert(QStringLiteral("notWrittenCount"), qint64(result.notWrittenFiles.size()));
    result.data.insert(QStringLiteral("failedCount"), qint64(result.failedFiles.size()));
    result.data.insert(QStringLiteral("sizeTotal"), sizeTotal);
    result.data.insert(QStringLiteral("fileNames"), QJsonArray::fromStringList(fileNames));
    result.data.insert(QStringLiteral("format"), QString::fromLatin1(format.format));
    result.data.insert(QStringLiteral("durationMilliseconds"), duration);
    return result;
}

QVariantMap PDFDocumentToolPdfToImages::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    using Helper = PDFDocumentToolPdfToImagesHelper;

    if (!from || !to || from == to)
    {
        return parameters;
    }

    // A page named by its number follows its page object (as Split)
    QVariantMap mapped = parameters;
    const QString key = Helper::name(PARAMETER_PAGES);
    QString error;
    if (mapped.contains(key))
    {
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(mapped.value(key), &error);
        if (selection)
        {
            mapped.insert(key, selection->mapPages(from, PDFDocumentToolPageSelection::mapPageIndices(from, to)).toVariant());
        }
    }
    return mapped;
}

}   // namespace pdf
