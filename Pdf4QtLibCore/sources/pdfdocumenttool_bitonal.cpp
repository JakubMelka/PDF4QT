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

#include "pdfdocumenttool_bitonal.h"

#include "pdfcms.h"
#include "pdfoperationcontrol.h"
#include "pdfrenderer.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <map>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the tool Convert to Black & White
class PDFDocumentToolBitonalHelper
{
public:
    PDFDocumentToolBitonalHelper() = delete;

    /// Most numbers of items listed in the fact "notConverted" (the rest is "…")
    static constexpr int MAXIMUM_LISTED_ITEMS = 20;

    /// Name of a parameter
    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Every item of the run and its mode: the pages or the convertible images of the document
    /// in their order, the mode of the last entry of "items" (convert without one)
    static std::vector<PDFBitonalDocumentCreator::ItemInfo> createItems(const PDFDocument* document,
                                                                        bool isPageSource,
                                                                        const std::vector<PDFObjectReference>& images,
                                                                        const std::vector<PDFDocumentToolBitonal::ItemEntry>& entries)
    {
        std::vector<PDFBitonalDocumentCreator::ItemInfo> items;
        if (isPageSource)
        {
            const size_t pageCount = document->getCatalog()->getPageCount();
            items.resize(pageCount);
            for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
            {
                items[pageIndex].pageIndex = PDFInteger(pageIndex);
            }

            for (const PDFDocumentToolBitonal::ItemEntry& entry : entries)
            {
                if (entry.isPage() && size_t(entry.pageIndex) < pageCount)
                {
                    items[size_t(entry.pageIndex)].mode = entry.mode;
                }
            }
            return items;
        }

        std::map<PDFObjectReference, size_t> indices;
        for (const PDFObjectReference reference : images)
        {
            PDFBitonalDocumentCreator::ItemInfo item;
            item.imageReference = reference;
            indices[reference] = items.size();
            items.push_back(item);
        }

        for (const PDFDocumentToolBitonal::ItemEntry& entry : entries)
        {
            auto it = !entry.isPage() ? indices.find(entry.image.reference) : indices.end();
            if (it != indices.end())
            {
                items[it->second].mode = entry.mode;
            }
        }
        return items;
    }

    /// "17, 43, 44" of the numbers (at most MAXIMUM_LISTED_ITEMS, then "…")
    static QString createNumberList(std::vector<PDFInteger> numbers)
    {
        std::sort(numbers.begin(), numbers.end());

        QStringList parts;
        for (const PDFInteger number : numbers)
        {
            if (parts.size() == MAXIMUM_LISTED_ITEMS)
            {
                parts << QStringLiteral("…");
                break;
            }
            parts << QString::number(number);
        }
        return parts.join(QStringLiteral(", "));
    }
};

QString PDFDocumentToolBitonal::getModeId(ItemMode mode)
{
    switch (mode)
    {
        case ItemMode::Algorithm:
            return QString::fromLatin1(MODE_CONVERT);
        case ItemMode::AlgorithmInverted:
            return QString::fromLatin1(MODE_CONVERT_INVERTED);
        case ItemMode::Original:
            return QString::fromLatin1(MODE_UNCHANGED);
        case ItemMode::FillBlack:
            return QString::fromLatin1(MODE_FILL_BLACK);
        case ItemMode::FillWhite:
            return QString::fromLatin1(MODE_FILL_WHITE);
    }

    Q_ASSERT(false);
    return QString();
}

std::optional<PDFDocumentToolBitonal::ItemMode> PDFDocumentToolBitonal::getMode(const QString& modeId)
{
    for (const ItemMode mode : { ItemMode::Algorithm, ItemMode::AlgorithmInverted, ItemMode::Original, ItemMode::FillBlack, ItemMode::FillWhite })
    {
        if (modeId == getModeId(mode))
        {
            return mode;
        }
    }
    return std::nullopt;
}

PDFImageConversion::ConversionMethod PDFDocumentToolBitonal::getConversionMethod(const QString& methodId)
{
    if (methodId == QLatin1String(METHOD_THRESHOLD))
    {
        return PDFImageConversion::ConversionMethod::Manual;
    }
    if (methodId == QLatin1String(METHOD_ADAPTIVE))
    {
        return PDFImageConversion::ConversionMethod::Adaptive;
    }
    if (methodId == QLatin1String(METHOD_DITHER))
    {
        return PDFImageConversion::ConversionMethod::Dither;
    }
    return PDFImageConversion::ConversionMethod::Automatic;
}

PDFBitonalDocumentCreator::Compression PDFDocumentToolBitonal::getCompression(const QString& compressionId)
{
    using Compression = PDFBitonalDocumentCreator::Compression;

    if (compressionId == QLatin1String(COMPRESSION_FLATE))
    {
        return Compression::Flate;
    }
    if (compressionId == QLatin1String(COMPRESSION_RUN_LENGTH))
    {
        return Compression::RunLength;
    }
    if (compressionId == QLatin1String(COMPRESSION_CCITT_G4))
    {
        return Compression::CCITTGroup4;
    }
    if (compressionId == QLatin1String(COMPRESSION_JBIG2))
    {
        return Compression::JBIG2;
    }
    return Compression::Auto;
}

std::optional<std::vector<PDFDocumentToolBitonal::ItemEntry>> PDFDocumentToolBitonal::parseItems(const QString& text, QString* errorMessage)
{
    std::vector<ItemEntry> entries;
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;

    const QStringList parts = text.split(QLatin1Char(';'));
    for (const QString& part : parts)
    {
        const QString entryText = part.trimmed();
        if (entryText.isEmpty())
        {
            continue;
        }

        const qsizetype separator = entryText.lastIndexOf(QLatin1Char('='));
        if (separator < 0)
        {
            error = tr("\"%1\" is not \"<page or image id>=<mode>\".").arg(entryText);
            return std::nullopt;
        }

        const QString target = entryText.left(separator).trimmed();
        const QString modeId = entryText.mid(separator + 1).trimmed();

        ItemEntry entry;
        const std::optional<ItemMode> mode = getMode(modeId);
        if (!mode)
        {
            error = tr("\"%1\" is no mode; the modes are convert, convert-inverted, unchanged, fill-black and fill-white.").arg(modeId);
            return std::nullopt;
        }
        entry.mode = *mode;

        bool isNumber = false;
        const qint64 pageNumber = target.toLongLong(&isNumber);
        if (isNumber)
        {
            if (pageNumber < 1)
            {
                error = tr("Page %1 is not in the document; pages are numbered from 1.").arg(target);
                return std::nullopt;
            }
            entry.pageIndex = PDFInteger(pageNumber - 1);
        }
        else
        {
            const std::optional<PDFDocumentImageList::Key> key = PDFDocumentImageList::Key::fromString(target);
            if (!key)
            {
                error = tr("\"%1\" is neither a page number nor an image id (\"148 0\").").arg(target);
                return std::nullopt;
            }
            if (key->isInline())
            {
                // R4-8: an inline image is never changed (a change would rewrite the content)
                error = tr("The inline image %1 is kept; only image objects are converted.").arg(target);
                return std::nullopt;
            }
            entry.image = *key;
        }

        entries.push_back(entry);
    }

    return entries;
}

QString PDFDocumentToolBitonal::createItemsText(const std::vector<ItemEntry>& entries)
{
    QStringList parts;
    for (const ItemEntry& entry : entries)
    {
        const QString target = entry.isPage() ? QString::number(entry.pageIndex + 1) : entry.image.toString();
        parts << QStringLiteral("%1=%2").arg(target, getModeId(entry.mode));
    }
    return parts.join(QStringLiteral("; "));
}

QString PDFDocumentToolBitonal::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolBitonal::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    // The texts of the options are the rows of the menus of 306:5353
    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_SOURCE), QT_TR_NOOP("Source"), QT_TR_NOOP("What is converted"),
                                                 {
                                                     { QString::fromLatin1(SOURCE_IMAGES), QT_TR_NOOP("Images"), QT_TR_NOOP("Each image is converted separately") },
                                                     { QString::fromLatin1(SOURCE_PAGES), QT_TR_NOOP("Whole pages"), QT_TR_NOOP("Pages are rasterized and converted as one image – for scans stored as several layers") },
                                                 },
                                                 QString::fromLatin1(SOURCE_PAGES)));
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_METHOD), QT_TR_NOOP("Method"), QT_TR_NOOP("How gray tones become black or white"),
                                                 {
                                                     { QString::fromLatin1(METHOD_AUTOMATIC), QT_TR_NOOP("Automatic (Otsu's 1D method)"), QT_TR_NOOP("The threshold is computed for every item") },
                                                     { QString::fromLatin1(METHOD_THRESHOLD), QT_TR_NOOP("User-defined threshold"), QT_TR_NOOP("One threshold for all items") },
                                                     { QString::fromLatin1(METHOD_ADAPTIVE), QT_TR_NOOP("Adaptive thresholding"), QT_TR_NOOP("Handles uneven lighting and shadows") },
                                                     { QString::fromLatin1(METHOD_DITHER), QT_TR_NOOP("Dithering (Floyd-Steinberg)"), QT_TR_NOOP("Keeps shades of photographs as dot patterns") },
                                                 },
                                                 QString::fromLatin1(METHOD_AUTOMATIC)));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_THRESHOLD), QT_TR_NOOP("Threshold (0-255)"),
                                                    QT_TR_NOOP("Used by the user-defined threshold and by the dithering"), 128, 0, 255));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_DPI), QT_TR_NOOP("Resolution"),
                                                    QT_TR_NOOP("Dots per inch of the converted pages (24 to 600); 0 = estimated from the images of the document; used for whole pages only"), 0, 0, MAXIMUM_DPI));
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_COMPRESSION), QT_TR_NOOP("Image compression"), QT_TR_NOOP("Encoding of the black and white images"),
                                                 {
                                                     { QString::fromLatin1(COMPRESSION_AUTO), QT_TR_NOOP("Automatic (smallest result)"), QT_TR_NOOP("Tries every algorithm and keeps the smallest") },
                                                     { QString::fromLatin1(COMPRESSION_FLATE), QT_TR_NOOP("Flate"), nullptr },
                                                     { QString::fromLatin1(COMPRESSION_RUN_LENGTH), QT_TR_NOOP("RunLength"), nullptr },
                                                     { QString::fromLatin1(COMPRESSION_CCITT_G4), QT_TR_NOOP("CCITT Group 4"), nullptr },
                                                     { QString::fromLatin1(COMPRESSION_JBIG2), QT_TR_NOOP("JBIG2"), nullptr },
                                                 },
                                                 QString::fromLatin1(COMPRESSION_AUTO)));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_ITEMS), QT_TR_NOOP("Modes of the items"),
                                                   QT_TR_NOOP("Pages or images with another mode, separated by semicolons: \"<page>=<mode>\" or \"<image id>=<mode>\", the mode convert, convert-inverted, unchanged, fill-black or fill-white; the image id of document.list_images (\"148 0\"); empty = everything is converted"),
                                                   QString(), MAXIMUM_ITEMS_LENGTH).setStored(false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_REPLACE_BLANK_PAGES), QT_TR_NOOP("Replace blank pages"),
                                                 QT_TR_NOOP("Pages, which are a scan of a blank sheet, get a white fill (pages only)"), false));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolBitonal::checkGivenParameters(const QVariantMap& given) const
{
    using Helper = PDFDocumentToolBitonalHelper;

    // A value, which the source ignores, would be ignored silently: the assistant would report a
    // result it did not get
    std::vector<PDFDocumentToolParameterError> errors;
    const bool isImageSource = given.value(Helper::name(PARAMETER_SOURCE)).toString() == QLatin1String(SOURCE_IMAGES);
    if (isImageSource && given.contains(Helper::name(PARAMETER_DPI)))
    {
        errors.push_back({ Helper::name(PARAMETER_DPI), tr("dpi is used only with the source pages: set source to pages, or leave dpi out.") });
    }
    if (isImageSource && given.value(Helper::name(PARAMETER_REPLACE_BLANK_PAGES)).toBool())
    {
        errors.push_back({ Helper::name(PARAMETER_REPLACE_BLANK_PAGES), tr("replaceBlankPages is used only with the source pages: set source to pages, or leave replaceBlankPages out.") });
    }
    return errors;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolBitonal::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolBitonalHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (!errors.empty() || !document)
    {
        return errors;
    }

    const QString itemsParameter = Helper::name(PARAMETER_ITEMS);
    const bool isPageSource = parameters.value(Helper::name(PARAMETER_SOURCE)).toString() != QLatin1String(SOURCE_IMAGES);

    QString itemsError;
    const std::optional<std::vector<ItemEntry>> entries = parseItems(parameters.value(itemsParameter).toString(), &itemsError);
    if (!entries)
    {
        errors.push_back({ itemsParameter, itemsError });
        return errors;
    }

    const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
    for (const ItemEntry& entry : *entries)
    {
        if (entry.isPage() && !isPageSource)
        {
            errors.push_back({ itemsParameter, tr("Page %1 has a mode, but the source images converts images: use an image id (\"148 0\").").arg(entry.pageIndex + 1) });
            return errors;
        }
        if (!entry.isPage() && isPageSource)
        {
            errors.push_back({ itemsParameter, tr("The image %1 has a mode, but the source pages converts pages: use a page number.").arg(entry.image.toString()) });
            return errors;
        }
        if (entry.isPage() && entry.pageIndex >= pageCount)
        {
            errors.push_back({ itemsParameter, tr("Page %1 is not in the document (1–%2).").arg(entry.pageIndex + 1).arg(pageCount) });
            return errors;
        }
    }

    if (!isPageSource && parameters.value(Helper::name(PARAMETER_REPLACE_BLANK_PAGES)).toBool())
    {
        errors.push_back({ Helper::name(PARAMETER_REPLACE_BLANK_PAGES), tr("Blank pages are detected only when whole pages are converted.") });
        return errors;
    }

    if (isPageSource)
    {
        // B65, plan 4.4: a page, whose image would exceed the limits of the pixels, is an error of
        // the resolution (the user lowers it), never a silently smaller image
        int dpi = parameters.value(Helper::name(PARAMETER_DPI)).toInt();
        if (dpi == 0)
        {
            dpi = PDFBitonalDocumentCreator(document, nullptr, nullptr).getEstimatedDpiResolution();
        }
        else if (dpi < MINIMUM_DPI)
        {
            errors.push_back({ Helper::name(PARAMETER_DPI), tr("The resolution must be 0 (estimated) or from %1 to %2 dpi.").arg(MINIMUM_DPI).arg(MAXIMUM_DPI) });
            return errors;
        }

        const std::vector<PDFBitonalDocumentCreator::ItemInfo> items = Helper::createItems(document, true, { }, *entries);
        for (const PDFBitonalDocumentCreator::ItemInfo& item : items)
        {
            if (!PDFBitonalDocumentCreator::isConversionMode(item.mode))
            {
                continue;
            }

            const QString sizeError = PDFBitonalDocumentCreator::getPageImageSizeError(document->getCatalog()->getPage(size_t(item.pageIndex)), item.pageIndex + 1, dpi);
            if (!sizeError.isEmpty())
            {
                errors.push_back({ Helper::name(PARAMETER_DPI), sizeError });
                return errors;
            }
        }
    }

    return errors;
}

PDFDocumentToolResult PDFDocumentToolBitonal::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    // R4-10: the color management of the settings of the application (the gray levels of the
    // colors before the threshold), empty = the default; the layers by OCUsage::Export always
    // (R4-10 a: the result does not depend on the layers shown in the tab) and the features of
    // PDFBitonalDocumentCreator::getPageRasterizationFeatures
    return convert(context, parameters, context.rendering.cmsSettings.value_or(PDFCMSSettings()), control, progress);
}

PDFDocumentToolResult PDFDocumentToolBitonal::convert(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFCMSSettings& cmsSettings,
                                                      const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolBitonalHelper;
    using Creator = PDFBitonalDocumentCreator;

    QElapsedTimer timer;
    timer.start();

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const bool isPageSource = parameters.value(Helper::name(PARAMETER_SOURCE)).toString() != QLatin1String(SOURCE_IMAGES);
    const bool isReplacingBlankPages = isPageSource && parameters.value(Helper::name(PARAMETER_REPLACE_BLANK_PAGES)).toBool();

    QString itemsError;
    const std::optional<std::vector<ItemEntry>> entries = parseItems(parameters.value(Helper::name(PARAMETER_ITEMS)).toString(), &itemsError);
    if (!entries)
    {
        return PDFDocumentToolResult::createFailed(itemsError);
    }

    const qint64 sizeBefore = getWholeFileInputSize(input);
    if (sizeBefore < 0)
    {
        return PDFDocumentToolResult::createFailed(tr("The size of the document cannot be measured."));
    }

    // One bar over the phases; the weights are the usual shares of the time
    PDFProgressPhases phases(progress);
    const int findPhase = !isPageSource ? phases.addPhase(5, tr("Looking for images"), false) : -1;
    const int blankPhase = isReplacingBlankPages ? phases.addPhase(25, tr("Detecting blank pages"), true) : -1;
    const int convertPhase = phases.addPhase(isPageSource ? 60 : 85, isPageSource ? tr("Converting pages") : tr("Converting images"), true);
    const int measurePhase = phases.addPhase(10, tr("Measuring the result"), false);
    phases.start();

    // The images of the run and their numbers ("Image N" of the list of the panel is the index + 1
    // of the whole list, inline images included)
    std::vector<PDFObjectReference> images;
    std::map<PDFObjectReference, PDFInteger> imageNumbers;
    if (!isPageSource)
    {
        phases.beginPhase(findPhase);
        const PDFDocumentImageList imageList = PDFDocumentImageList::create(document, PDFDocumentImageList::Options::createForChange(), control, nullptr);
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }

        for (size_t index = 0; index < imageList.getItems().size(); ++index)
        {
            const PDFDocumentImageList::Item& item = imageList.getItems()[index];
            if (Creator::isConvertibleImage(item))
            {
                images.push_back(item.key.reference);
                imageNumbers[item.key.reference] = PDFInteger(index + 1);
            }
        }

        // An id of the items, which is no convertible image, would be ignored silently
        for (const ItemEntry& entry : *entries)
        {
            if (!imageNumbers.count(entry.image.reference))
            {
                const PDFDocumentImageList::Item* item = imageList.findItem(entry.image);
                return PDFDocumentToolResult::createFailed(item ? tr("The image %1 cannot be converted: it is a stencil mask, the mask of another image or a part of the appearance of an annotation.").arg(entry.image.toString())
                                                                : tr("%1 is not an image drawn in the document.").arg(entry.image.toString()));
            }
        }

        if (images.empty())
        {
            return PDFDocumentToolResult::createNothingToDo(tr("The document has no images, which can be converted."));
        }
    }

    std::vector<Creator::ItemInfo> items = Helper::createItems(document, isPageSource, images, *entries);

    // The pages are rendered with the layers of an export (R4-10 a) and the budget of the pool
    // (Q4.7); the images need no rendering
    std::unique_ptr<PDFBitonalRasterizationEnvironment> environment;
    if (isPageSource)
    {
        environment = std::make_unique<PDFBitonalRasterizationEnvironment>(document, cmsSettings, PDFRasterizerPool::getDefaultRasterizerCount());
    }

    Creator creator(document, environment ? environment->getRasterizerPool() : nullptr, phases.getPhase(convertPhase));

    // Blank pages (MCP and PdfTool: the detection and the white fill in one run); only the pages,
    // which would be converted, are examined - a page left unchanged or filled stays so
    std::vector<PDFInteger> blankPages;
    if (isReplacingBlankPages)
    {
        std::vector<PDFInteger> examinedPages;
        for (const Creator::ItemInfo& item : items)
        {
            if (Creator::isConversionMode(item.mode))
            {
                examinedPages.push_back(item.pageIndex);
            }
        }

        const std::vector<Creator::BlankPageResult> results = creator.detectBlankPages(examinedPages, control, phases.getPhase(blankPhase));
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }

        for (const Creator::BlankPageResult& result : results)
        {
            if (result.isBlank)
            {
                blankPages.push_back(result.pageIndex);
                items[size_t(result.pageIndex)].mode = ItemMode::FillWhite;
            }
        }
    }

    const bool hasReplacedItem = std::any_of(items.cbegin(), items.cend(), [](const Creator::ItemInfo& item) { return item.isContentReplaced(); });
    if (!hasReplacedItem)
    {
        return PDFDocumentToolResult::createNothingToDo(isPageSource ? tr("Every page is left unchanged – nothing is converted.") : tr("Every image is left unchanged – nothing is converted."));
    }

    Creator::Settings settings;
    settings.conversionSource = isPageSource ? Creator::ConversionSource::Pages : Creator::ConversionSource::Images;
    settings.conversionMethod = getConversionMethod(parameters.value(Helper::name(PARAMETER_METHOD)).toString());
    settings.manualThreshold = parameters.value(Helper::name(PARAMETER_THRESHOLD)).toInt();
    settings.compression = getCompression(parameters.value(Helper::name(PARAMETER_COMPRESSION)).toString());
    const int dpi = parameters.value(Helper::name(PARAMETER_DPI)).toInt();
    settings.dpiResolution = dpi > 0 ? dpi : creator.getEstimatedDpiResolution();
    settings.items = items;

    phases.beginPhase(convertPhase);
    const bool isConverted = creator.createBitonalDocument(settings, control);
    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!isConverted)
    {
        // 307:6388
        return PDFDocumentToolResult::createFailed(tr("The bitonal document has not been created. No item of the document could be converted."));
    }

    // The document is written whole (LostCopyOnly) and its content is not checked against a
    // standard it declares: the declaration goes (a page of an image has no tags)
    PDFDocument converted = creator.takeBitonalDocument();
    ConformanceRemoval conformance;
    PDFDocument output = createWholeFileDocument(converted.getStorage(), document, &conformance);
    if (!conformance.kept.isEmpty())
    {
        return createConformanceFailure(conformance.kept);
    }

    phases.beginPhase(measurePhase);
    const WholeFileSize outputSize = getWholeFileOutputSize(&output);
    if (!outputSize.error.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(outputSize.error);
    }
    const qint64 sizeAfter = outputSize.size;
    phases.finish();

    // The counts by the modes: an item, which has failed, is counted only as not converted
    std::set<std::pair<PDFInteger, PDFObjectReference>> failedKeys;
    for (const Creator::FailedItem& failed : creator.getFailedItems())
    {
        failedKeys.insert({ failed.item.pageIndex, failed.item.imageReference });
    }

    qint64 convertedCount = 0;
    qint64 invertedCount = 0;
    qint64 whiteCount = 0;
    qint64 blackCount = 0;
    qint64 unchangedCount = 0;
    for (const Creator::ItemInfo& item : items)
    {
        if (failedKeys.count({ item.pageIndex, item.imageReference }))
        {
            continue;
        }

        switch (item.mode)
        {
            case ItemMode::Algorithm:
                ++convertedCount;
                break;
            case ItemMode::AlgorithmInverted:
                ++invertedCount;
                break;
            case ItemMode::FillWhite:
                ++whiteCount;
                break;
            case ItemMode::FillBlack:
                ++blackCount;
                break;
            case ItemMode::Original:
                ++unchangedCount;
                break;
        }
    }

    // 307:6564 "117 converted · 1 filled white · 2 unchanged" - a count, which is zero, is left out
    // (the converted ones are always said); the inverted ones are converted too
    QStringList itemsParts;
    itemsParts << tr("%Ln converted", nullptr, int(convertedCount + invertedCount));
    if (whiteCount > 0)
    {
        itemsParts << tr("%Ln filled white", nullptr, int(whiteCount));
    }
    if (blackCount > 0)
    {
        itemsParts << tr("%Ln filled black", nullptr, int(blackCount));
    }
    if (unchangedCount > 0)
    {
        itemsParts << tr("%Ln unchanged", nullptr, int(unchangedCount));
    }
    if (!creator.getFailedItems().empty())
    {
        itemsParts << tr("%Ln not converted", nullptr, int(creator.getFailedItems().size()));
    }

    // The compressions, which the converted items have got ("JBIG2"; the automatic one can choose
    // more of them, the most used first)
    std::vector<std::pair<size_t, Creator::Compression>> compressions;
    for (const auto& [compression, count] : creator.getUsedCompressions())
    {
        compressions.push_back({ count, compression });
    }
    std::sort(compressions.begin(), compressions.end(), [](const auto& left, const auto& right) { return left.first > right.first; });
    QStringList compressionNames;
    QJsonObject compressionCounts;
    for (const auto& [count, compression] : compressions)
    {
        compressionNames << Creator::getCompressionName(compression);
        compressionCounts.insert(Creator::getCompressionName(compression), qint64(count));
    }

    const int savedPercent = PDFDocumentToolFormat::getSavedPercent(sizeBefore, sizeAfter);
    QString sizeValue = sizeAfter < sizeBefore ? tr("%1 (%2 % smaller)").arg(PDFDocumentToolFormat::formatSizeChange(sizeBefore, sizeAfter)).arg(savedPercent)
                                               : PDFDocumentToolFormat::formatSizeChange(sizeBefore, sizeAfter);
    if (!compressionNames.isEmpty())
    {
        sizeValue = tr("%1 · %2").arg(sizeValue, compressionNames.join(QStringLiteral(", ")));
    }

    PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("items"), isPageSource ? tr("Pages") : tr("Images"), itemsParts.join(QStringLiteral(" · ")) });
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), sizeValue });

    // 307:6388 "Not converted: pages 17, 43, 44 (rendering errors)": by the reasons in their order
    QJsonArray notConverted;
    if (!creator.getFailedItems().empty())
    {
        std::map<Creator::FailureReason, std::vector<PDFInteger>> numbersByReason;
        for (const Creator::FailedItem& failed : creator.getFailedItems())
        {
            const PDFInteger number = failed.item.pageIndex >= 0 ? failed.item.pageIndex + 1 : imageNumbers[failed.item.imageReference];
            numbersByReason[failed.reason].push_back(number);

            QJsonObject entry;
            if (failed.item.pageIndex >= 0)
            {
                entry.insert(QStringLiteral("page"), qint64(failed.item.pageIndex + 1));
            }
            else
            {
                entry.insert(QStringLiteral("image"), PDFDocumentImageList::Key::createReference(failed.item.imageReference).toString());
                entry.insert(QStringLiteral("imageNumber"), qint64(number));
            }
            entry.insert(QStringLiteral("reason"), Creator::getFailureReasonText(failed.reason));
            notConverted.append(entry);
        }

        QStringList parts;
        for (const auto& [reason, numbers] : numbersByReason)
        {
            const QString list = Helper::createNumberList(numbers);
            QString what;
            if (isPageSource)
            {
                what = numbers.size() == 1 ? tr("page %1").arg(list) : tr("pages %1").arg(list);
            }
            else
            {
                what = numbers.size() == 1 ? tr("image %1").arg(list) : tr("images %1").arg(list);
            }
            parts << tr("%1 (%2)").arg(what, Creator::getFailureReasonText(reason));
        }
        result.facts.push_back({ QStringLiteral("notConverted"), tr("Not converted"), parts.join(QStringLiteral("; ")) });
    }

    // Owner 14. 11. 2026: a rendering error does not block the conversion, the page is converted
    // and the user is asked to check it ("Rendering issues", as PDF to Images, 339:6505)
    QJsonArray renderingIssuePages;
    if (!creator.getRenderingIssues().empty())
    {
        std::vector<PDFInteger> numbers;
        for (const Creator::RenderingIssue& issue : creator.getRenderingIssues())
        {
            numbers.push_back(issue.pageIndex + 1);
            renderingIssuePages.append(qint64(issue.pageIndex + 1));
            for (const QString& message : issue.messages)
            {
                result.renderingIssues.push_back({ issue.pageIndex, message });
            }
        }

        const QString list = Helper::createNumberList(numbers);
        const QString value = numbers.size() == 1 ? tr("page %1 – check this page").arg(list) : tr("pages %1 – check these pages").arg(list);
        result.facts.push_back({ QStringLiteral("renderingIssues"), tr("Rendering issues"), value });
    }

    if (isReplacingBlankPages)
    {
        std::vector<PDFInteger> numbers;
        for (const PDFInteger pageIndex : blankPages)
        {
            numbers.push_back(pageIndex + 1);
        }
        result.facts.push_back({ QStringLiteral("blankPages"), tr("Blank pages"), blankPages.empty() ? tr("None found") : tr("%1 · filled white").arg(Helper::createNumberList(numbers)) });
    }

    // 8.11.4.4: a page image has no layers - what the layers of an export show is merged into it
    if (isPageSource && document->getCatalog()->getOptionalContentProperties()->isValid())
    {
        result.facts.push_back({ QStringLiteral("layers"), tr("Layers"), tr("Layers of converted pages are merged") });
    }

    const qint64 duration = timer.elapsed();
    result.facts.push_back({ QStringLiteral("time"), tr("Time"), PDFDocumentToolFormat::formatDuration(duration) });
    addConformanceFact(&result, conformance.removed);

    QJsonArray blankPageNumbers;
    for (const PDFInteger pageIndex : blankPages)
    {
        blankPageNumbers.append(qint64(pageIndex + 1));
    }

    result.data.insert(QStringLiteral("source"), isPageSource ? QString::fromLatin1(SOURCE_PAGES) : QString::fromLatin1(SOURCE_IMAGES));
    result.data.insert(QStringLiteral("dpi"), settings.dpiResolution);
    result.data.insert(QStringLiteral("converted"), convertedCount + invertedCount);
    result.data.insert(QStringLiteral("inverted"), invertedCount);
    result.data.insert(QStringLiteral("filledWhite"), whiteCount);
    result.data.insert(QStringLiteral("filledBlack"), blackCount);
    result.data.insert(QStringLiteral("unchanged"), unchangedCount);
    result.data.insert(QStringLiteral("notConverted"), notConverted);
    result.data.insert(QStringLiteral("renderingIssuePages"), renderingIssuePages);
    result.data.insert(QStringLiteral("compressions"), compressionCounts);
    if (isReplacingBlankPages)
    {
        result.data.insert(QStringLiteral("blankPages"), blankPageNumbers);
    }
    result.data.insert(QStringLiteral("durationMilliseconds"), duration);

    // The purpose is the black and white document, not only a smaller one: it is the result also,
    // when it is larger (a vector page becomes an image)
    finishWholeFileResult(&result, input, std::move(output), sizeBefore, sizeAfter, QStringLiteral("_bw"), WholeFileDocument::Always);
    return result;
}

}   // namespace pdf
