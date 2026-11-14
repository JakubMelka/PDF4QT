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

#include "pdfdocumenttool_extractimages.h"
#include "pdfdocumenttool_pdftoimages.h"
#include "pdfdocumenttool_split.h"

#include "pdfcatalog.h"
#include "pdfcolorspaces.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfimage.h"
#include "pdfimagecompressor.h"
#include "pdfoperationcontrol.h"
#include "pdfutils.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <limits>
#include <map>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of Extract Images
class PDFDocumentToolExtractImagesHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolExtractImages)

public:
    PDFDocumentToolExtractImagesHelper() = delete;

    using Key = PDFDocumentImageList::Key;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// What the walk of the pages found of an image: its color space resolved by the content
    /// (a resource name of the page or the form) and, for an inline image, a copy of its stream
    struct Source
    {
        PDFObject colorSpace;
        std::optional<PDFStream> inlineStream;
        bool isFound = false;
    };

    /// Walks the first page of each item (in the threads of the policy, a page per task; every
    /// layer, as the list) and takes what an item needs to be read without the content. Nothing
    /// is decoded. A canceled walk misses the rest.
    static std::map<Key, Source> findSources(const PDFDocument* document, const PDFDocumentImageList& list, const std::vector<size_t>& itemIndices, const PDFOperationControl* control)
    {
        std::map<Key, Source> sources;
        std::map<PDFInteger, std::set<Key>> keysOfPages;
        for (const size_t itemIndex : itemIndices)
        {
            const PDFDocumentImageList::Item& item = list.getItems()[itemIndex];
            sources[item.key] = Source();
            if (!item.pageIndices.empty())
            {
                keysOfPages[item.pageIndices.front()].insert(item.key);
            }
        }

        std::vector<std::pair<PDFInteger, std::map<Key, Source>>> pages;
        for (const auto& [pageIndex, keys] : keysOfPages)
        {
            Q_UNUSED(keys);
            pages.emplace_back(pageIndex, std::map<Key, Source>());
        }

        const PDFImageCompressor::Environment environment(document);
        const auto processPage = [&](std::pair<PDFInteger, std::map<Key, Source>>& page)
        {
            const std::set<Key>& keys = keysOfPages.at(page.first);
            std::map<Key, Source>& found = page.second;
            const PDFImageCompressor::DrawnImageCallback addSource = [&keys, &found](const PDFImageCompressor::DrawnImage& image)
            {
                const Key key = image.reference.isValid() ? Key::createReference(image.reference) : Key::createInline(image.inlineContentStream, image.inlineIndex);
                if (!keys.count(key) || found.count(key))
                {
                    return;
                }

                Source source;
                source.colorSpace = image.colorSpace;
                source.isFound = true;
                if (key.isInline() && image.stream)
                {
                    source.inlineStream = *image.stream;
                }
                found.emplace(key, std::move(source));
            };
            environment.processPageImages(page.first, addSource, nullptr, control);
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pages.begin(), pages.end(), processPage);

        for (auto& page : pages)
        {
            for (auto& [key, source] : page.second)
            {
                sources[key] = std::move(source);
            }
        }
        return sources;
    }

    /// The source of a key in the found sources: a reference into the map, or an empty source
    /// that lives as long as the program (never a temporary, see getStream)
    static const Source& findSource(const std::map<Key, Source>& sources, const Key& key)
    {
        static const Source empty;
        const auto it = sources.find(key);
        return it != sources.cend() ? it->second : empty;
    }

    /// The stream of an item (the image XObject, or the copy of an inline image), nullptr = none.
    /// The stream of an inline image points into \p source, so the source must outlive it.
    static const PDFStream* getStream(const PDFDocument* document, const Key& key, const Source& source)
    {
        if (key.isInline())
        {
            return source.inlineStream ? &*source.inlineStream : nullptr;
        }
        const PDFObject& object = document->getObjectByReference(key.reference);
        return object.isStream() ? object.getStream() : nullptr;
    }

    /// A temporary source would die before its stream is read (ASan, P2S4: a conditional
    /// operator of a found source and Source() made a temporary copy) - it does not compile
    static const PDFStream* getStream(const PDFDocument* document, const Key& key, Source&& source) = delete;

    /// The raw streams are the same image (B60): the same data and the same dictionary without
    /// /Length (the filter, its parameters, the size, the color space, the masks, /Decode)
    static bool isSameStream(const PDFStream* left, const PDFStream* right)
    {
        if (!left || !right || !left->getContent() || !right->getContent() || *left->getContent() != *right->getContent())
        {
            return false;
        }

        const PDFDictionary* leftDictionary = left->getDictionary();
        const PDFDictionary* rightDictionary = right->getDictionary();
        size_t leftCount = 0;
        for (size_t i = 0; i < leftDictionary->getCount(); ++i)
        {
            const QByteArray key = leftDictionary->getKey(i).getString();
            if (key == "Length")
            {
                continue;
            }
            ++leftCount;
            if (!rightDictionary->hasKey(key) || !(leftDictionary->getValue(i) == rightDictionary->get(key)))
            {
                return false;
            }
        }

        size_t rightCount = 0;
        for (size_t i = 0; i < rightDictionary->getCount(); ++i)
        {
            rightCount += rightDictionary->getKey(i).getString() != "Length" ? 1 : 0;
        }
        return leftCount == rightCount;
    }

    using PlanEntry = PDFDocumentToolExtractImages::ExportPlanCache::Entry;

    /// The stream of an entry of the plan (the image XObject, or the copy of an inline image in
    /// the entry), nullptr = none. The stream of an inline image points into \p entry.
    static const PDFStream* getStream(const PDFDocument* document, const Key& key, const PlanEntry& entry)
    {
        if (key.isInline())
        {
            return entry.inlineStream ? &*entry.inlineStream : nullptr;
        }
        const PDFObject& object = document->getObjectByReference(key.reference);
        return object.isStream() ? object.getStream() : nullptr;
    }

    /// A temporary entry would die before its stream is read - it does not compile
    static const PDFStream* getStream(const PDFDocument* document, const Key& key, PlanEntry&& entry) = delete;

    /// The entry of the plan of a key: from the cache, or computed (the hash of the raw data and
    /// the copy of the stream of an inline image found by the walk) and put into the cache
    static PlanEntry getPlanEntry(const PDFDocument* document, const Key& key, const std::map<Key, Source>& inlineSources, PDFDocumentToolExtractImages::ExportPlanCache* cache)
    {
        PlanEntry entry;
        if (cache && cache->find(key, &entry))
        {
            return entry;
        }

        if (key.isInline())
        {
            entry.inlineStream = findSource(inlineSources, key).inlineStream;
        }
        const PDFStream* stream = getStream(document, key, entry);
        if (stream && stream->getContent())
        {
            entry.hash = QCryptographicHash::hash(*stream->getContent(), QCryptographicHash::Sha256);
        }
        if (cache)
        {
            cache->insert(key, entry);
        }
        return entry;
    }

    /// The image is too large to be decoded or written in the format (plan 6, R4-19): the reason
    static QString checkSize(const PDFDocumentImageList::Item& item, const PDFDocumentToolImageFormat& format)
    {
        const QSize size = item.pixelSize;
        if (qint64(size.width()) * qint64(size.height()) * 4 > PDFDocumentToolImageFormat::MAXIMUM_IMAGE_BYTES)
        {
            return tr("The image is too large: %1 × %2 px").arg(QLocale().toString(size.width()), QLocale().toString(size.height()));
        }
        const int maximumSide = getMaximumSide(format);
        if (size.width() > maximumSide || size.height() > maximumSide)
        {
            return tr("The image is %1 × %2 px, %3 holds at most %4 px per side").arg(QLocale().toString(size.width()), QLocale().toString(size.height()),
                                                                                      format.getDisplayName(), QLocale().toString(maximumSide));
        }
        return QString();
    }

    /// Most pixels of a side of an extracted image. The side limit of R4-19 (32 767 px) is the one
    /// of the raster engine of Qt, but an extracted PNG or TIFF image is written without painting
    /// (it keeps its alpha), so only the 2 GiB of the decoded image applies (owner 2026-11-14).
    /// The other formats keep their limit: WebP its own, and a format without alpha may paint
    /// the image over white in PDFDocumentToolImageFormat::encode.
    static int getMaximumSide(const PDFDocumentToolImageFormat& format)
    {
        static const std::set<QByteArray> withoutSideLimit = { "png", "tiff", "tif" };
        if (withoutSideLimit.count(format.format) > 0)
        {
            return (std::numeric_limits<int>::max)();
        }
        return format.getMaximumSide();
    }

    /// Decodes the stream of an image as the page shows it (the color management, the soft mask
    /// and the mask as alpha); a null image and the reason
    static QImage decode(const PDFDocument* document, const PDFStream* stream, const PDFObject& colorSpaceObject, const PDFCMS* cms, const PDFOperationControl* control, QString* errorMessage)
    {
        if (!stream)
        {
            *errorMessage = tr("The image cannot be read.");
            return QImage();
        }

        try
        {
            PDFRenderErrorReporterDummy reporter;
            PDFColorSpacePointer colorSpace;
            const PDFObject& object = document->getObject(colorSpaceObject);
            if (object.isName() || object.isArray())
            {
                colorSpace = PDFAbstractColorSpace::createColorSpace(nullptr, document, nullptr, object);
            }

            const PDFImage image = PDFImage::createImage(document, stream, std::move(colorSpace), false, RenderingIntent::Perceptual, &reporter);
            QImage result = image.getImage(cms, &reporter, control);
            if (result.isNull() || result.format() == QImage::Format_Alpha8)
            {
                *errorMessage = tr("The image cannot be read.");
                return QImage();
            }
            return result;
        }
        catch (const PDFException& exception)
        {
            *errorMessage = tr("The image cannot be read: %1").arg(exception.getMessage());
            return QImage();
        }
        catch (const PDFRendererException& exception)
        {
            // The decoder reports a damaged or unsupported image so (an explicit /Mask, which is
            // no stencil mask, 8.9.6.3) - an image of the export, which failed with its reason
            *errorMessage = tr("The image cannot be read: %1").arg(exception.getError().message);
            return QImage();
        }
    }

    /// The color management of a context: the given one, else the default settings
    static PDFCMSPointer getCMS(const PDFDocument* document, PDFCMSPointer cms)
    {
        if (cms)
        {
            return cms;
        }
        PDFCMSManager manager(nullptr);
        manager.setDocument(document);
        manager.setSettings(manager.getDefaultSettings());
        return manager.getCurrentCMS();
    }

    /// One image decoded and encoded by a task
    struct Encoded
    {
        QByteArray data;
        QString errorMessage;
    };
};

// ------------------------------------------------------- PDFDocumentToolExtractImages::ExportPlan

QString PDFDocumentToolExtractImages::ExportPlan::getFileName(size_t itemIndex) const
{
    for (const Image& image : images)
    {
        if (image.itemIndex == itemIndex)
        {
            return image.fileName;
        }
    }
    return QString();
}

// ------------------------------------------------------- PDFDocumentToolExtractImages

QString PDFDocumentToolExtractImages::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolExtractImages::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages, whose images are exported")).setStored(false));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_FORMAT), QT_TR_NOOP("Format"),
                                                   QT_TR_NOOP("Image format: png, jpeg, tiff, webp, bmp or another format of the image writer"), QString::fromLatin1(DEFAULT_FORMAT), 32));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_COMPRESSION), QT_TR_NOOP("Compression"),
                                                    QT_TR_NOOP("Compression level of the format; -1 = the default of the format"), -1, -1, 100));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_QUALITY), QT_TR_NOOP("Quality"),
                                                    QT_TR_NOOP("Quality of a lossy format, 0 to 100; -1 = the default of the format"), -1, -1, 100));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_OPTIMIZED_WRITE), QT_TR_NOOP("Optimized write"),
                                                 QT_TR_NOOP("Optimize the written file (JPEG only)"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_PROGRESSIVE_SCAN_WRITE), QT_TR_NOOP("Progressive scan write"),
                                                 QT_TR_NOOP("Write a progressive image (JPEG only)"), false));

    // The left out images belong to one document
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_EXCLUDE), QT_TR_NOOP("Left out images"),
                                                   QT_TR_NOOP("Images, which are not exported, separated by commas: the ids of document.list_images (\"148 0\", \"inline:12 0:3\"); empty = every image"),
                                                   QString(), MAXIMUM_EXCLUDE_LENGTH).setStored(false));
    PDFDocumentToolFileOutput::addDefinitions(&definitions, tr, QString::fromLatin1(DEFAULT_FILE_NAME));
    PDFDocumentToolFileOutput::addOnlyOutputsDefinition(&definitions, tr);
    return definitions;
}

std::vector<PDFDocumentImageList::Key> PDFDocumentToolExtractImages::parseKeys(const QString& text, QString* errorMessage)
{
    errorMessage->clear();
    std::vector<PDFDocumentImageList::Key> keys;
    std::set<PDFDocumentImageList::Key> known;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        if (part.trimmed().isEmpty())
        {
            continue;
        }

        const std::optional<PDFDocumentImageList::Key> key = PDFDocumentImageList::Key::fromString(part);
        if (!key)
        {
            *errorMessage = tr("\"%1\" is no id of an image: use the ids of document.list_images (\"148 0\"), separated by commas.").arg(part.trimmed());
            return {};
        }
        if (known.insert(*key).second)
        {
            keys.push_back(*key);
        }
    }
    return keys;
}

QString PDFDocumentToolExtractImages::writeKeys(const std::vector<PDFDocumentImageList::Key>& keys)
{
    QStringList texts;
    for (const PDFDocumentImageList::Key& key : keys)
    {
        texts << key.toString();
    }
    return texts.join(QStringLiteral(", "));
}

PDFDocumentImageList PDFDocumentToolExtractImages::createImageList(const PDFDocument* document, const std::optional<std::map<PDFObjectReference, OCState>>& layerStates,
                                                                  const PDFOperationControl* control, PDFProgressSink* progress)
{
    const PDFDocumentImageList::Options options = layerStates ? PDFDocumentImageList::Options::createForExport(*layerStates) : PDFDocumentImageList::Options::createForExport();
    return PDFDocumentImageList::create(document, options, control, progress);
}

bool PDFDocumentToolExtractImages::ExportPlanCache::find(const PDFDocumentImageList::Key& key, Entry* entry) const
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_entries.find(key);
    if (it == m_entries.cend())
    {
        return false;
    }
    *entry = it->second;
    return true;
}

bool PDFDocumentToolExtractImages::ExportPlanCache::contains(const PDFDocumentImageList::Key& key) const
{
    QMutexLocker locker(&m_mutex);
    return m_entries.count(key) > 0;
}

void PDFDocumentToolExtractImages::ExportPlanCache::insert(const PDFDocumentImageList::Key& key, Entry entry)
{
    QMutexLocker locker(&m_mutex);
    m_entries.insert_or_assign(key, std::move(entry));
}

size_t PDFDocumentToolExtractImages::ExportPlanCache::getCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_entries.size();
}

bool PDFDocumentToolExtractImages::isExportPlanParameter(const QString& name)
{
    // The names of the files (the template and the extension of the format), the pages and the
    // left out images; never the encoding (compression, quality, ...), the folder or the policy
    for (const char* parameter : { PARAMETER_PAGES, PARAMETER_EXCLUDE, PARAMETER_FORMAT, PDFDocumentToolFileOutput::PARAMETER_FILE_NAME })
    {
        if (name == QLatin1String(parameter))
        {
            return true;
        }
    }
    return false;
}

std::optional<PDFDocumentToolExtractImages::ExportPlan> PDFDocumentToolExtractImages::createExportPlan(const PDFDocument* document, const PDFDocumentImageList& list, const QString& sourceFileName,
                                                                                                     const QVariantMap& parameters, const QDate& date, const PDFOperationControl* control,
                                                                                                     ExportPlanCache* cache, QString* errorMessage)
{
    using Helper = PDFDocumentToolExtractImagesHelper;

    const std::vector<PDFInteger> pages = PDFDocumentToolPdfToImages::getPages(document, parameters, errorMessage);
    if (pages.empty())
    {
        return std::nullopt;
    }
    const std::set<PDFInteger> selectedPages(pages.cbegin(), pages.cend());

    const std::vector<PDFDocumentImageList::Key> excludedKeys = parseKeys(parameters.value(Helper::name(PARAMETER_EXCLUDE)).toString(), errorMessage);
    if (!errorMessage->isEmpty())
    {
        return std::nullopt;
    }
    for (const PDFDocumentImageList::Key& key : excludedKeys)
    {
        if (list.indexOf(key) < 0)
        {
            *errorMessage = tr("Image %1 is not an image of the document. Use the ids of document.list_images.").arg(key.toString());
            return std::nullopt;
        }
    }
    const std::set<PDFDocumentImageList::Key> excluded(excludedKeys.cbegin(), excludedKeys.cend());

    ExportPlan plan;
    std::vector<size_t> candidates;
    const std::vector<PDFDocumentImageList::Item>& items = list.getItems();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const PDFDocumentImageList::Item& item = items[i];
        const bool isOnSelectedPage = std::any_of(item.pageIndices.cbegin(), item.pageIndices.cend(), [&selectedPages](PDFInteger page) { return selectedPages.count(page) > 0; });
        if (item.isImageMask)
        {
            // A stencil has no picture of its own: the color of the page paints through it (8.9.6.2)
            plan.stencils.push_back(i);
        }
        else if (!isOnSelectedPage || excluded.count(item.key))
        {
            plan.excluded.push_back(i);
        }
        else
        {
            candidates.push_back(i);
        }
    }

    // B60: the same image once - by the reference (one item each), then by the raw stream. The
    // streams of the inline images are read from the content (a walk of their first pages), only
    // of those, which the cache of the list does not have yet.
    std::vector<size_t> inlineItems;
    for (const size_t itemIndex : candidates)
    {
        if (items[itemIndex].key.isInline() && !(cache && cache->contains(items[itemIndex].key)))
        {
            inlineItems.push_back(itemIndex);
        }
    }
    const std::map<PDFDocumentImageList::Key, Helper::Source> inlineSources = inlineItems.empty() ? std::map<PDFDocumentImageList::Key, Helper::Source>() : Helper::findSources(document, list, inlineItems, control);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return std::nullopt;
    }

    // The entries (the hash and the stream) of the candidates, in their order; a canceled plan
    // stops between two images (the hash of all the streams of a large file takes seconds)
    std::vector<Helper::PlanEntry> entries;
    entries.reserve(candidates.size());
    for (const size_t itemIndex : candidates)
    {
        if (PDFOperationControl::isOperationCancelled(control))
        {
            return std::nullopt;
        }
        entries.push_back(Helper::getPlanEntry(document, items[itemIndex].key, inlineSources, cache));
    }

    std::map<QByteArray, std::vector<size_t>> imagesOfHash;    // hash of the data -> indices into plan.images
    std::vector<size_t> entryOfImage;                           // index into entries of each image of plan.images
    for (size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex)
    {
        const size_t itemIndex = candidates[candidateIndex];
        const Helper::PlanEntry& entry = entries[candidateIndex];
        const PDFStream* stream = Helper::getStream(document, items[itemIndex].key, entry);

        bool isDuplicate = false;
        if (!entry.hash.isEmpty())
        {
            for (const size_t imageIndex : imagesOfHash[entry.hash])
            {
                ExportPlan::Image& image = plan.images[imageIndex];
                const PDFDocumentImageList::Item& original = items[image.itemIndex];
                if (Helper::isSameStream(stream, Helper::getStream(document, original.key, entries[entryOfImage[imageIndex]])))
                {
                    image.duplicates.push_back(itemIndex);
                    plan.duplicates.push_back(itemIndex);
                    isDuplicate = true;
                    break;
                }
            }
        }

        if (!isDuplicate)
        {
            if (!entry.hash.isEmpty())
            {
                imagesOfHash[entry.hash].push_back(plan.images.size());
            }
            ExportPlan::Image image;
            image.itemIndex = itemIndex;
            plan.images.push_back(std::move(image));
            entryOfImage.push_back(candidateIndex);
        }
    }

    // K4.12: "#" is the number of the image in the list - a left out number is skipped
    if (plan.images.size() > size_t(PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT))
    {
        *errorMessage = tr("More than %Ln images would be exported.", nullptr, PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT);
        return std::nullopt;
    }

    const PDFFileNameTemplate nameTemplate(parameters.value(Helper::name(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME)).toString(), PDFDocumentToolImageFormat::fromParameters(parameters).getExtension());
    if (!nameTemplate.isValid(errorMessage))
    {
        return std::nullopt;
    }

    QStringList names;
    const QString sourceName = sourceFileName.isEmpty() ? QStringLiteral("document.pdf") : sourceFileName;
    for (ExportPlan::Image& image : plan.images)
    {
        const PDFDocumentImageList::Item& item = items[image.itemIndex];
        PDFFileNameTemplate::Values values;
        values.outputIndex = int(image.itemIndex + 1);
        values.outputCount = int(qMin<size_t>(items.size(), size_t(std::numeric_limits<int>::max())));
        values.pageNumber = item.pageIndices.empty() ? 0 : item.pageIndices.front() + 1;
        values.sourceIndex = 1;
        values.groupIndex = values.outputIndex;
        values.sourceFileName = sourceName;
        values.date = date;
        image.fileName = nameTemplate.createFileName(values);
        names << image.fileName;
    }

    const std::vector<int> duplicateNames = PDFFileNameTemplate::findDuplicates(names);
    if (!duplicateNames.empty())
    {
        *errorMessage = int(duplicateNames.size()) + 1 == int(names.size()) ? tr("Every file would be named %1").arg(names.front())
                                                                            : tr("%Ln files would have the same name as another file", nullptr, int(duplicateNames.size()));
        return std::nullopt;
    }
    return plan;
}

QImage PDFDocumentToolExtractImages::readImage(const PDFDocument* document, const PDFDocumentImageList::Item& item, PDFCMSPointer cms, const PDFOperationControl* control, QString* errorMessage)
{
    using Helper = PDFDocumentToolExtractImagesHelper;

    if (!document || item.isImageMask)
    {
        *errorMessage = tr("The image cannot be read.");
        return QImage();
    }

    const QString sizeError = Helper::checkSize(item, PDFDocumentToolImageFormat());
    if (!sizeError.isEmpty())
    {
        *errorMessage = sizeError;
        return QImage();
    }

    // The first page of the item gives its color space and the stream of an inline image
    Helper::Source source;
    const PDFImageCompressor::Environment environment(document);
    const PDFImageCompressor::DrawnImageCallback addSource = [&item, &source](const PDFImageCompressor::DrawnImage& image)
    {
        const PDFDocumentImageList::Key key = image.reference.isValid() ? PDFDocumentImageList::Key::createReference(image.reference) : PDFDocumentImageList::Key::createInline(image.inlineContentStream, image.inlineIndex);
        if (source.isFound || key != item.key)
        {
            return;
        }
        source.colorSpace = image.colorSpace;
        source.isFound = true;
        if (key.isInline() && image.stream)
        {
            source.inlineStream = *image.stream;
        }
    };
    if (!item.pageIndices.empty())
    {
        environment.processPageImages(item.pageIndices.front(), addSource, nullptr, control);
    }

    const PDFStream* stream = Helper::getStream(document, item.key, source);
    const PDFObject colorSpace = source.isFound ? source.colorSpace : (stream ? stream->getDictionary()->get("ColorSpace") : PDFObject());
    const PDFCMSPointer usedCms = Helper::getCMS(document, std::move(cms));
    return Helper::decode(document, stream, colorSpace, usedCms.data(), control, errorMessage);
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolExtractImages::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolExtractImagesHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (!errors.empty() || !document)
    {
        return errors;
    }

    QString errorMessage;
    if (PDFDocumentToolPdfToImages::getPages(document, parameters, &errorMessage).empty())
    {
        errors.push_back({ Helper::name(PARAMETER_PAGES), errorMessage });
    }
    if (!PDFDocumentToolImageFormat::fromParameters(parameters).isValid(&errorMessage))
    {
        errors.push_back({ Helper::name(PARAMETER_FORMAT), errorMessage });
    }
    parseKeys(parameters.value(Helper::name(PARAMETER_EXCLUDE)).toString(), &errorMessage);
    if (!errorMessage.isEmpty())
    {
        errors.push_back({ Helper::name(PARAMETER_EXCLUDE), errorMessage });
    }
    if (!PDFFileNameTemplate(parameters.value(Helper::name(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME)).toString(), QString()).isValid(&errorMessage))
    {
        errors.push_back({ Helper::name(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), errorMessage });
    }

    // The list of the images walks the pages: the plan (the ids, the equal names) is checked by the run
    return errors;
}

PDFDocumentToolResult PDFDocumentToolExtractImages::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolExtractImagesHelper;
    using Encoded = Helper::Encoded;

    QElapsedTimer timer;
    timer.start();

    PDFDocumentToolFileSink* sink = context.fileSink;
    if (!sink)
    {
        return PDFDocumentToolResult::createFailed(tr("The files have no place to be written."));
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const PDFDocumentToolImageFormat format = PDFDocumentToolImageFormat::fromParameters(parameters);
    QString errorMessage;
    if (!format.isValid(&errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    PDFProgressPhases phases(progress);
    const int listPhase = phases.addPhase(10, tr("Looking for images"), false);
    const int writePhase = phases.addPhase(90, tr("Writing images"), true);
    phases.start();

    const PDFDocumentImageList list = createImageList(document, context.rendering.layerStates, control, phases.getPhase(listPhase));
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // Retry N Files writes the files of the earlier run: the date of {date} is the date of that run
    // (the plans of the tried dates read the hashes of the images once, from one cache)
    ExportPlanCache planCache;
    const QDate runDate = PDFDocumentToolPdfToImages::getRunDate(parameters, [&](const QDate& date)
    {
        QString ignoredError;
        const std::optional<ExportPlan> datePlan = createExportPlan(document, list, input.fileName, parameters, date, control, &planCache, &ignoredError);
        QStringList names;
        if (datePlan)
        {
            for (const ExportPlan::Image& image : datePlan->images)
            {
                names << image.fileName;
            }
        }
        return names;
    });
    const std::optional<ExportPlan> plan = createExportPlan(document, list, input.fileName, parameters, runDate, control, &planCache, &errorMessage);
    if (!plan)
    {
        return PDFOperationControl::isOperationCancelled(control) ? PDFDocumentToolResult::createStopped() : PDFDocumentToolResult::createFailed(errorMessage);
    }
    if (plan->images.empty())
    {
        // Nothing on the pages is a result (341:8976 "No images on page 308"), leaving every image out is a mistake
        if (parseKeys(parameters.value(QString::fromLatin1(PARAMETER_EXCLUDE)).toString(), &errorMessage).empty())
        {
            return PDFDocumentToolResult::createNothingToDo(tr("The selected pages have no images."));
        }
        return PDFDocumentToolResult::createFailed(tr("No image is chosen for the export."));
    }

    // Retry N Files (K4.11): only the names of the earlier run, which failed
    const QStringList onlyOutputs = PDFDocumentToolFileOutput::getOnlyOutputs(parameters);
    std::vector<const ExportPlan::Image*> images;
    QStringList fileNames;
    for (const ExportPlan::Image& image : plan->images)
    {
        if (onlyOutputs.isEmpty() || onlyOutputs.contains(image.fileName))
        {
            images.push_back(&image);
            fileNames << image.fileName;
        }
    }
    if (images.empty())
    {
        return PDFDocumentToolResult::createFailed(tr("None of the files to write again belongs to this export."));
    }

    if (!sink->begin(fileNames, &errorMessage))
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    std::vector<size_t> itemIndices;
    itemIndices.reserve(images.size());
    for (const ExportPlan::Image* image : images)
    {
        itemIndices.push_back(image->itemIndex);
    }
    const std::map<PDFDocumentImageList::Key, Helper::Source> sources = Helper::findSources(document, list, itemIndices, control);
    const PDFCMSPointer cms = Helper::getCMS(document, context.cms);

    PDFProgressSink* writeProgress = phases.getPhase(writePhase);
    phases.beginPhase(writePhase);
    if (writeProgress)
    {
        writeProgress->start(images.size(), ProgressStartupInfo());
    }

    // The images are decoded and encoded in the threads of the policy, a chunk at a time, and
    // written in their order by this thread (the sink has one thread): the memory holds at most a
    // chunk of encoded files and the decoded images of the tasks running at once
    const size_t chunkSize = size_t(qMax(1, 2 * PDFExecutionPolicy::getMaxThreadCount(PDFExecutionPolicy::Scope::Page)));
    qint64 sizeTotal = 0;
    std::vector<PDFDocumentToolFileProblem> failures;
    bool isStopped = false;
    size_t handledCount = 0;
    for (size_t chunkStart = 0; chunkStart < images.size() && !isStopped; chunkStart += chunkSize)
    {
        const size_t chunkEnd = qMin(images.size(), chunkStart + chunkSize);
        std::vector<Encoded> encoded(chunkEnd - chunkStart);
        PDFIntegerRange<size_t> range(chunkStart, chunkEnd);
        const auto encodeImage = [&](size_t index)
        {
            if (PDFOperationControl::isOperationCancelled(control))
            {
                return;
            }

            const PDFDocumentImageList::Item& item = list.getItems()[images[index]->itemIndex];
            Encoded& target = encoded[index - chunkStart];
            target.errorMessage = Helper::checkSize(item, format);
            if (!target.errorMessage.isEmpty())
            {
                return;
            }

            const Helper::Source& source = Helper::findSource(sources, item.key);
            const PDFStream* stream = Helper::getStream(document, item.key, source);
            const PDFObject colorSpace = source.isFound ? source.colorSpace : (stream ? stream->getDictionary()->get("ColorSpace") : PDFObject());
            const QImage image = Helper::decode(document, stream, colorSpace, cms.data(), control, &target.errorMessage);
            if (!image.isNull())
            {
                target.data = format.encode(image, &target.errorMessage);
            }
        };
        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, range.begin(), range.end(), encodeImage);

        for (size_t index = chunkStart; index < chunkEnd; ++index)
        {
            if (PDFOperationControl::isOperationCancelled(control) || sink->isStopped())
            {
                isStopped = true;
                break;
            }

            Encoded& current = encoded[index - chunkStart];
            const QString& fileName = fileNames[qsizetype(index)];
            ++handledCount;
            if (current.data.isEmpty())
            {
                failures.push_back({ fileName, current.errorMessage.isEmpty() ? tr("The image cannot be written.") : current.errorMessage });
            }
            else
            {
                QString writeError;
                if (sink->write(fileName, current.data, &writeError))
                {
                    sizeTotal += current.data.size();
                }
            }
            current = Encoded();

            if (writeProgress)
            {
                writeProgress->step();
            }
        }
    }
    isStopped = isStopped || PDFOperationControl::isOperationCancelled(control) || sink->isStopped();

    PDFDocumentToolResult result;
    result.status = isStopped ? PDFDocumentToolResultStatus::Stopped : PDFDocumentToolResultStatus::Success;
    result.writtenFiles = sink->getWrittenFiles();
    result.notWrittenFiles = sink->getNotWrittenFiles();
    for (const auto& [fileName, reason] : sink->getFailedFiles())
    {
        result.failedFiles.push_back({ fileName, reason });
    }
    for (const PDFDocumentToolFileProblem& failure : failures)
    {
        result.failedFiles.push_back(failure);
        result.notWrittenFiles << failure.fileName;
    }
    for (size_t index = handledCount; index < images.size(); ++index)
    {
        result.notWrittenFiles << fileNames[qsizetype(index)];
    }

    const qint64 duration = timer.elapsed();
    const int writtenCount = int(result.writtenFiles.size());
    const QString formatName = format.getDisplayName();
    const QString sizeText = PDFDocumentToolFormat::formatSize(sizeTotal);
    if (isStopped)
    {
        result.facts.push_back({ QStringLiteral("written"), tr("Written"), tr("%Ln %1 · %2", nullptr, writtenCount).arg(formatName, sizeText) });
        result.facts.push_back({ QStringLiteral("notExported"), tr("Not exported"), tr("%Ln images", nullptr, int(images.size() - qMin(images.size(), size_t(writtenCount)))) });
    }
    else if (!result.failedFiles.empty())
    {
        result.facts.push_back({ QStringLiteral("written"), tr("Written"), tr("%Ln %1 · %2", nullptr, writtenCount).arg(formatName, sizeText) });
        result.facts.push_back({ QStringLiteral("failed"), tr("Failed"), tr("%Ln files", nullptr, int(result.failedFiles.size())) });
    }
    else
    {
        // 341:8462: Files "409 PNG · numbered like the images", Left out "3 images, by your choice"
        result.facts.push_back({ QStringLiteral("files"), tr("Files"), tr("%Ln %1 · numbered like the images", nullptr, writtenCount).arg(formatName) });
    }

    QStringList leftOut;
    if (!plan->excluded.empty())
    {
        leftOut << tr("%Ln images, by your choice", nullptr, int(plan->excluded.size()));
    }
    if (!plan->duplicates.empty())
    {
        leftOut << tr("%Ln identical, exported once", nullptr, int(plan->duplicates.size()));
    }
    if (!plan->stencils.empty())
    {
        leftOut << tr("%Ln masks without a picture", nullptr, int(plan->stencils.size()));
    }
    if (!leftOut.isEmpty())
    {
        result.facts.push_back({ QStringLiteral("leftOut"), tr("Left out"), leftOut.join(QStringLiteral(" · ")) });
    }
    if (!isStopped && result.failedFiles.empty())
    {
        result.facts.push_back({ QStringLiteral("size"), tr("Total size"), sizeText });
        if (!result.notWrittenFiles.isEmpty())
        {
            result.facts.push_back({ QStringLiteral("notWritten"), tr("Not written"), tr("%Ln · %1", nullptr, int(result.notWrittenFiles.size())).arg(PDFDocumentToolFileOutput::formatFileNames(result.notWrittenFiles)) });
        }
    }
    result.facts.push_back({ QStringLiteral("time"), tr("Time"), PDFDocumentToolFormat::formatDuration(duration) });

    QJsonArray exported;
    for (const ExportPlan::Image& image : plan->images)
    {
        const PDFDocumentImageList::Item& item = list.getItems()[image.itemIndex];
        QJsonArray pageNumbers;
        for (const PDFInteger pageIndex : item.pageIndices)
        {
            pageNumbers.append(qint64(pageIndex + 1));
        }

        QJsonObject object;
        object.insert(QStringLiteral("id"), item.key.toString());
        object.insert(QStringLiteral("number"), qint64(image.itemIndex + 1));
        object.insert(QStringLiteral("fileName"), image.fileName);
        object.insert(QStringLiteral("pages"), pageNumbers);
        object.insert(QStringLiteral("width"), qint64(item.pixelSize.width()));
        object.insert(QStringLiteral("height"), qint64(item.pixelSize.height()));
        object.insert(QStringLiteral("streamBytes"), qint64(item.streamBytes));
        QJsonArray duplicates;
        for (const size_t duplicate : image.duplicates)
        {
            duplicates.append(list.getItems()[duplicate].key.toString());
        }
        object.insert(QStringLiteral("duplicates"), duplicates);
        exported.append(object);
    }

    result.data.insert(QStringLiteral("imageCount"), qint64(list.getItems().size()));
    result.data.insert(QStringLiteral("exportedCount"), qint64(plan->images.size()));
    result.data.insert(QStringLiteral("excludedCount"), qint64(plan->excluded.size()));
    result.data.insert(QStringLiteral("duplicateCount"), qint64(plan->duplicates.size()));
    result.data.insert(QStringLiteral("writtenCount"), qint64(writtenCount));
    result.data.insert(QStringLiteral("notWrittenCount"), qint64(result.notWrittenFiles.size()));
    result.data.insert(QStringLiteral("failedCount"), qint64(result.failedFiles.size()));
    result.data.insert(QStringLiteral("sizeTotal"), sizeTotal);
    result.data.insert(QStringLiteral("fileNames"), QJsonArray::fromStringList(fileNames));
    result.data.insert(QStringLiteral("format"), QString::fromLatin1(format.format));
    result.data.insert(QStringLiteral("images"), exported);
    result.data.insert(QStringLiteral("durationMilliseconds"), duration);
    return result;
}

QVariantMap PDFDocumentToolExtractImages::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    // The pages follow their page objects (as PDF to Images); the ids of the images are references
    return PDFDocumentToolPdfToImages().mapParameters(parameters, from, to);
}

}   // namespace pdf
