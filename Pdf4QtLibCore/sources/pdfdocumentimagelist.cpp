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

#include "pdfdocumentimagelist.h"

#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfexecutionpolicy.h"
#include "pdfimagecompressor.h"
#include "pdfimageoptimizer.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfutils.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

#include "pdfdbgheap.h"

namespace pdf
{

/// Helpers of the collection of the images
class PDFDocumentImageListHelper
{
public:
    PDFDocumentImageListHelper() = delete;

    /// One drawing of an image found on a page (in the order of the content)
    struct Drawing
    {
        PDFDocumentImageList::Key key;
        PDFDocumentImageList::Item description;  ///< The values read from the dictionary (no pages, no resolution yet)
        QPointF dpi;                            ///< Resolution of this drawing per axis (0 = degenerated)
    };

    /// What one page draws
    struct PageImages
    {
        std::vector<Drawing> drawings;          ///< The drawings in the order of the content, all layers
        std::set<PDFDocumentImageList::Key> visibleKeys;   ///< Keys drawn in a visible layer (only when the hidden layers are computed)
        bool isWalked = false;                  ///< The page was walked (not skipped by the cancellation)
    };

    /// The resolution of pixels over a length in inches, 0 for a degenerated length
    static double getDpi(int pixels, double inches)
    {
        return inches > 0.0 && pixels > 0 ? double(pixels) / inches : 0.0;
    }

    /// The smaller of two resolutions, a resolution of 0 is unknown
    static double getMinimumDpi(double current, double candidate)
    {
        if (candidate <= 0.0 || !std::isfinite(candidate))
        {
            return current;
        }
        return current <= 0.0 ? candidate : std::min(current, candidate);
    }

    /// Bytes of the encoded data of a stream object (a reference), 0 = none
    static qint64 getStreamBytes(const PDFDocument* document, const PDFObject& object)
    {
        const PDFObject& dereferenced = document->getObject(object);
        if (dereferenced.isStream() && dereferenced.getStream()->getContent())
        {
            return dereferenced.getStream()->getContent()->size();
        }
        return 0;
    }

    /// The name of the family of a declared color space ("Indexed" of an array)
    static QString getColorSpaceName(const PDFDocument* document, const PDFObject& colorSpace)
    {
        const PDFObject& object = document->getObject(colorSpace);
        if (object.isName())
        {
            return QString::fromLatin1(object.getString());
        }
        if (object.isArray() && object.getArray()->getCount() > 0)
        {
            const PDFObject& family = document->getObject(object.getArray()->getItem(0));
            if (family.isName())
            {
                return QString::fromLatin1(family.getString());
            }
        }
        return QString();
    }

    /// The declared color space is a space of gray tones (R4-5)
    static bool isGrayColorSpace(const PDFDocument* document, const PDFObject& colorSpace, int depth)
    {
        if (depth > PDFDocumentImageList::MAXIMUM_COLOR_SPACE_DEPTH)
        {
            return false;
        }

        const PDFObject& object = document->getObject(colorSpace);
        if (object.isName())
        {
            // The device space and its abbreviation of an inline image (Table 92); CalGray is
            // always an array, a name of an unknown space is no gray
            const QByteArray name = object.getString();
            return name == "DeviceGray" || name == "G";
        }

        if (!object.isArray() || object.getArray()->getCount() == 0)
        {
            return false;
        }

        const PDFArray* array = object.getArray();
        const PDFObject& familyObject = document->getObject(array->getItem(0));
        if (!familyObject.isName())
        {
            return false;
        }

        const QByteArray family = familyObject.getString();
        if (family == "DeviceGray" || family == "G" || family == "CalGray")
        {
            return true;
        }

        if (family == "ICCBased")
        {
            // 8.6.5.5, Table 66: /N is the number of the components; without it the alternate space
            const PDFObject& profile = array->getCount() > 1 ? document->getObject(array->getItem(1)) : PDFObject();
            if (!profile.isStream())
            {
                return false;
            }

            const PDFDictionary* profileDictionary = profile.getStream()->getDictionary();
            const PDFObject& components = document->getObject(profileDictionary->get("N"));
            if (components.isInt())
            {
                return components.getInteger() == 1;
            }
            return isGrayColorSpace(document, profileDictionary->get("Alternate"), depth + 1);
        }

        if (family == "Indexed" || family == "I")
        {
            // 8.6.6.3: [/Indexed base hival lookup] - the colors of the table are of the base
            return array->getCount() > 1 && isGrayColorSpace(document, array->getItem(1), depth + 1);
        }

        // DeviceRGB, DeviceCMYK, CalRGB, Lab, Separation, DeviceN, Pattern and unknown families
        return false;
    }

    /// The components of the base space of a palette (8.6.6.3): how a gray entry of it looks
    enum class PaletteBase
    {
        Gray,   ///< One gray component: every entry is gray
        Rgb,    ///< Red, green, blue: an entry of three equal components is gray
        Cmyk,   ///< Cyan, magenta, yellow, black: an entry with only black is gray
        Other   ///< Lab, Separation, DeviceN, an unknown space: no entry is taken as gray
    };

    /// The base of a palette by its family (an ICCBased space by /N, without it by /Alternate)
    static PaletteBase getPaletteBase(const PDFDocument* document, const PDFObject& colorSpace, int depth)
    {
        if (depth > PDFDocumentImageList::MAXIMUM_COLOR_SPACE_DEPTH)
        {
            return PaletteBase::Other;
        }

        const PDFObject& object = document->getObject(colorSpace);
        const PDFArray* array = object.isArray() ? object.getArray() : nullptr;
        if (array && array->getCount() == 0)
        {
            return PaletteBase::Other;
        }
        const PDFObject& familyObject = array ? document->getObject(array->getItem(0)) : object;
        if (!familyObject.isName())
        {
            return PaletteBase::Other;
        }

        const QByteArray family = familyObject.getString();
        if (family == "DeviceGray" || family == "G" || family == "CalGray")
        {
            return PaletteBase::Gray;
        }
        if (family == "DeviceRGB" || family == "RGB" || family == "CalRGB")
        {
            return PaletteBase::Rgb;
        }
        if (family == "DeviceCMYK" || family == "CMYK")
        {
            return PaletteBase::Cmyk;
        }
        if (family == "ICCBased" && array && array->getCount() > 1)
        {
            // 8.6.5.5, Table 66: /N is the number of the components
            const PDFObject& profile = document->getObject(array->getItem(1));
            if (!profile.isStream())
            {
                return PaletteBase::Other;
            }
            const PDFDictionary* profileDictionary = profile.getStream()->getDictionary();
            const PDFObject& components = document->getObject(profileDictionary->get("N"));
            if (components.isInt())
            {
                switch (components.getInteger())
                {
                    case 1:
                        return PaletteBase::Gray;
                    case 3:
                        return PaletteBase::Rgb;
                    case 4:
                        return PaletteBase::Cmyk;
                    default:
                        return PaletteBase::Other;
                }
            }
            return getPaletteBase(document, profileDictionary->get("Alternate"), depth + 1);
        }
        return PaletteBase::Other;
    }

    /// The entries 0 and 1 of an Indexed space (the two values of an image of one bit, 8.6.6.3:
    /// [/Indexed base hival lookup], the lookup has m × (hival + 1) bytes) are gray tones. A
    /// palette, which cannot be read, a missing entry or a base, whose gray is not known, is not
    /// gray (the image is converted).
    static bool isGrayPaletteOfOneBit(const PDFDocument* document, const PDFArray* indexed, int depth)
    {
        if (indexed->getCount() < 4)
        {
            return false;
        }

        const PaletteBase base = getPaletteBase(document, indexed->getItem(1), depth + 1);
        if (base == PaletteBase::Gray || base == PaletteBase::Other)
        {
            return base == PaletteBase::Gray;
        }

        const PDFObject& highValue = document->getObject(indexed->getItem(2));
        if (!highValue.isInt() || highValue.getInteger() < 0)
        {
            return false;
        }

        QByteArray lookup;
        const PDFObject& lookupObject = document->getObject(indexed->getItem(3));
        if (lookupObject.isString())
        {
            lookup = lookupObject.getString();
        }
        else if (lookupObject.isStream())
        {
            try
            {
                lookup = document->getDecodedStream(lookupObject.getStream());
            }
            catch (const PDFException&)
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        const qsizetype components = base == PaletteBase::Rgb ? 3 : 4;
        const qsizetype entries = qsizetype(std::min<PDFInteger>(highValue.getInteger(), 1)) + 1;
        if (lookup.size() < entries * components)
        {
            return false;
        }

        for (qsizetype entry = 0; entry < entries; ++entry)
        {
            const uchar* values = reinterpret_cast<const uchar*>(lookup.constData()) + entry * components;
            const bool isGray = base == PaletteBase::Rgb ? (values[0] == values[1] && values[1] == values[2]) : (values[0] == 0 && values[1] == 0 && values[2] == 0);
            if (!isGray)
            {
                return false;
            }
        }
        return true;
    }

    /// The images of an XObject of an appearance: an image (its masks too), or the images of the
    /// resources of a form
    static void collectAppearanceXObject(const PDFDocument* document, const PDFObject& object, int depth, PDFObjectWalk* walk, std::set<PDFObjectReference>* images)
    {
        if (!walk->enter(object, depth))
        {
            return;
        }

        const PDFObject& dereferenced = document->getObject(object);
        if (!dereferenced.isStream())
        {
            return;
        }

        const PDFDictionary* dictionary = dereferenced.getStream()->getDictionary();
        const PDFObject& subtype = document->getObject(dictionary->get("Subtype"));
        if (subtype.isName() && subtype.getString() == "Image")
        {
            if (object.isReference())
            {
                images->insert(object.getReference());
            }

            // Its masks belong to the appearance too (8.9.6.3, 8.9.6.4)
            for (const char* key : { "SMask", "Mask" })
            {
                const PDFObject& mask = dictionary->get(key);
                if (mask.isReference() && document->getObject(mask).isStream())
                {
                    images->insert(mask.getReference());
                }
            }
            return;
        }

        // A form (an appearance stream is a form without /Subtype too, 12.5.5)
        collectAppearanceResources(document, dictionary->get("Resources"), depth + 1, walk, images);
    }

    /// The images of a resource dictionary of an appearance: its XObjects, the tiling patterns, the
    /// groups of the soft masks of the graphics states (11.6.5.2) and the Type 3 fonts
    static void collectAppearanceResources(const PDFDocument* document, const PDFObject& resources, int depth, PDFObjectWalk* walk, std::set<PDFObjectReference>* images)
    {
        if (!walk->enter(resources, depth))
        {
            return;
        }

        const PDFObject& resourcesObject = document->getObject(resources);
        if (!resourcesObject.isDictionary())
        {
            return;
        }
        const PDFDictionary* resourcesDictionary = resourcesObject.getDictionary();

        for (const char* category : { "XObject", "Pattern", "ExtGState", "Font" })
        {
            const PDFObject& categoryObject = resourcesDictionary->get(category);
            if (!walk->enter(categoryObject, depth + 1))
            {
                continue;
            }
            const PDFObject& categoryDereferenced = document->getObject(categoryObject);
            if (!categoryDereferenced.isDictionary())
            {
                continue;
            }

            const PDFDictionary* entries = categoryDereferenced.getDictionary();
            for (size_t i = 0; i < entries->getCount(); ++i)
            {
                const PDFObject& entry = entries->getValue(i);
                if (qstrcmp(category, "XObject") == 0)
                {
                    collectAppearanceXObject(document, entry, depth + 2, walk, images);
                    continue;
                }

                if (!walk->enter(entry, depth + 2))
                {
                    continue;
                }
                const PDFObject& value = document->getObject(entry);
                const PDFDictionary* valueDictionary = value.isStream() ? value.getStream()->getDictionary() : (value.isDictionary() ? value.getDictionary() : nullptr);
                if (!valueDictionary)
                {
                    continue;
                }

                if (qstrcmp(category, "ExtGState") == 0)
                {
                    // /SMask of a graphics state is a dictionary with the group /G (Table 142)
                    const PDFObject& softMask = valueDictionary->get("SMask");
                    if (!walk->enter(softMask, depth + 3))
                    {
                        continue;
                    }
                    const PDFObject& softMaskObject = document->getObject(softMask);
                    if (softMaskObject.isDictionary())
                    {
                        collectAppearanceXObject(document, softMaskObject.getDictionary()->get("G"), depth + 4, walk, images);
                    }
                    continue;
                }

                // A tiling pattern (a stream) and a Type 3 font have resources of their own
                collectAppearanceResources(document, valueDictionary->get("Resources"), depth + 3, walk, images);
            }
        }
    }

    /// An image of one bit is bitonal (owner 14. 11., R4-5): a one-component gray space, or an
    /// Indexed space, whose two used entries are gray. 1 bit of a color space (DeviceRGB has 8
    /// colors, a red and blue palette, a Separation) is a color image.
    static bool isBitonalSpaceOfOneBit(const PDFDocument* document, const PDFObject& colorSpace)
    {
        const PDFObject& object = document->getObject(colorSpace);
        if (object.isArray() && object.getArray()->getCount() > 0)
        {
            const PDFObject& family = document->getObject(object.getArray()->getItem(0));
            if (family.isName() && (family.getString() == "Indexed" || family.getString() == "I"))
            {
                return isGrayPaletteOfOneBit(document, object.getArray(), 0);
            }
        }
        return getPaletteBase(document, colorSpace, 0) == PaletteBase::Gray;
    }

    /// The values of an item read from the dictionary of an image stream
    static PDFDocumentImageList::Item describe(const PDFDocument* document, const PDFImageCompressor::DrawnImage& image)
    {
        PDFDocumentImageList::Item item;
        const PDFDictionary* dictionary = image.stream->getDictionary();
        PDFDocumentDataLoaderDecorator loader(document);

        const PDFInteger width = loader.readIntegerFromDictionary(dictionary, "Width", 0);
        const PDFInteger height = loader.readIntegerFromDictionary(dictionary, "Height", 0);
        constexpr PDFInteger maximumSide = std::numeric_limits<int>::max();
        item.pixelSize = QSize(int(qBound<PDFInteger>(0, width, maximumSide)), int(qBound<PDFInteger>(0, height, maximumSide)));
        item.isImageMask = loader.readBooleanFromDictionary(dictionary, "ImageMask", false);
        const PDFInteger bitsPerComponent = loader.readIntegerFromDictionary(dictionary, "BitsPerComponent", item.isImageMask ? PDFInteger(1) : PDFInteger(0));
        item.bitsPerComponent = int(std::clamp(bitsPerComponent, PDFInteger(0), PDFInteger(64)));
        item.colorSpaceName = getColorSpaceName(document, image.colorSpace);
        item.color = PDFDocumentImageList::getImageColor(document, dictionary, image.colorSpace);
        item.filterName = PDFImageOptimizer::readFilterName(document, dictionary);
        item.streamBytes = image.stream->getContent() ? qint64(image.stream->getContent()->size()) : 0;

        const PDFObject& softMask = dictionary->get("SMask");
        if (softMask.isReference())
        {
            item.softMaskReference = softMask.getReference();
        }
        item.maskBytes += getStreamBytes(document, softMask);

        // /Mask is an explicit mask (a stream) or a color key mask (an array, no bytes, 8.9.6.4)
        const PDFObject& mask = dictionary->get("Mask");
        if (mask.isReference() && document->getObject(mask).isStream())
        {
            item.maskReference = mask.getReference();
            item.maskBytes += getStreamBytes(document, mask);
        }
        return item;
    }
};

PDFDocumentImageList::Key PDFDocumentImageList::Key::createReference(PDFObjectReference reference)
{
    Key key;
    key.reference = reference;
    return key;
}

PDFDocumentImageList::Key PDFDocumentImageList::Key::createInline(PDFObjectReference contentStream, PDFInteger index)
{
    Key key;
    key.inlineContentStream = contentStream;
    key.inlineIndex = index;
    return key;
}

QString PDFDocumentImageList::Key::toString() const
{
    if (isInline())
    {
        return QStringLiteral("inline:%1 %2:%3").arg(inlineContentStream.objectNumber).arg(inlineContentStream.generation).arg(inlineIndex);
    }
    return QStringLiteral("%1 %2").arg(reference.objectNumber).arg(reference.generation);
}

std::optional<PDFDocumentImageList::Key> PDFDocumentImageList::Key::fromString(QStringView text)
{
    static const QRegularExpression referenceExpression(QStringLiteral("^\\s*(\\d{1,10})\\s+(\\d{1,10})\\s*$"));
    static const QRegularExpression inlineExpression(QStringLiteral("^\\s*inline:\\s*(\\d{1,10})\\s+(\\d{1,10})\\s*:\\s*(\\d{1,10})\\s*$"));

    // A number of a reference is at most 2^31 - 1, also in a valid file (7.3.10: a positive integer)
    const auto readNumber = [](QStringView number, PDFInteger* value) -> bool
    {
        bool isOk = false;
        const qint64 result = number.toLongLong(&isOk);
        if (!isOk || result < 0 || result > std::numeric_limits<int>::max())
        {
            return false;
        }
        *value = result;
        return true;
    };

    QRegularExpressionMatch match = inlineExpression.matchView(text);
    if (match.hasMatch())
    {
        PDFInteger objectNumber = 0;
        PDFInteger generation = 0;
        PDFInteger index = 0;
        if (!readNumber(match.capturedView(1), &objectNumber) || !readNumber(match.capturedView(2), &generation) || !readNumber(match.capturedView(3), &index) || objectNumber == 0)
        {
            return std::nullopt;
        }
        return createInline(PDFObjectReference(objectNumber, generation), index);
    }

    match = referenceExpression.matchView(text);
    if (match.hasMatch())
    {
        PDFInteger objectNumber = 0;
        PDFInteger generation = 0;
        if (!readNumber(match.capturedView(1), &objectNumber) || !readNumber(match.capturedView(2), &generation) || objectNumber == 0)
        {
            return std::nullopt;
        }
        return createReference(PDFObjectReference(objectNumber, generation));
    }
    return std::nullopt;
}

bool PDFDocumentImageList::Key::operator==(const Key& other) const
{
    return reference == other.reference && inlineContentStream == other.inlineContentStream && inlineIndex == other.inlineIndex;
}

bool PDFDocumentImageList::Key::operator<(const Key& other) const
{
    return std::tie(inlineIndex, reference, inlineContentStream) < std::tie(other.inlineIndex, other.reference, other.inlineContentStream);
}

PDFDocumentImageList::Options PDFDocumentImageList::Options::createForChange()
{
    return Options();
}

PDFDocumentImageList::Options PDFDocumentImageList::Options::createForExport()
{
    Options options;
    options.hiddenLayers = HiddenLayers::ExportUsage;
    return options;
}

PDFDocumentImageList::Options PDFDocumentImageList::Options::createForExport(std::map<PDFObjectReference, OCState> layerStates)
{
    Options options;
    options.hiddenLayers = HiddenLayers::GivenStates;
    options.layerStates = std::move(layerStates);
    return options;
}

PDFDocumentImageList PDFDocumentImageList::create(const PDFDocument* document, const Options& options, const PDFOperationControl* operationControl, PDFProgressSink* progress)
{
    using Helper = PDFDocumentImageListHelper;

    PDFDocumentImageList list;
    if (!document || !document->getCatalog())
    {
        return list;
    }

    const size_t pageCount = document->getCatalog()->getPageCount();
    if (progress && pageCount > 0)
    {
        progress->start(pageCount, ProgressStartupInfo());
    }

    // The layers of the second walk, which tells the drawings in visible layers: created here, only
    // read by the threads of the pages (no state changes during the walk)
    std::unique_ptr<PDFOptionalContentActivity> visibleLayers;
    switch (options.hiddenLayers)
    {
        case Options::HiddenLayers::NotComputed:
            break;

        case Options::HiddenLayers::ExportUsage:
            visibleLayers = std::make_unique<PDFOptionalContentActivity>(document, OCUsage::Export, nullptr);
            break;

        case Options::HiddenLayers::GivenStates:
            visibleLayers = std::make_unique<PDFOptionalContentActivity>(document, OCUsage::View, nullptr);
            for (const auto& [ocg, state] : options.layerStates)
            {
                // The states of the tab exactly as they are (radio button groups are already applied)
                visibleLayers->setState(ocg, state, false);
            }
            break;
    }

    PDFImageCompressor::Environment environment(document);
    std::vector<Helper::PageImages> pages(pageCount);
    QMutex progressMutex;

    const auto processPage = [&](size_t pageIndex)
    {
        if (PDFOperationControl::isOperationCanceled(operationControl))
        {
            return;
        }

        Helper::PageImages& pageImages = pages[pageIndex];
        const PDFImageCompressor::DrawnImageCallback addDrawing = [&pageImages, document](const PDFImageCompressor::DrawnImage& image)
        {
            Helper::Drawing drawing;
            drawing.key = image.reference.isValid() ? Key::createReference(image.reference) : Key::createInline(image.inlineContentStream, image.inlineIndex);
            if (!drawing.key.isValid())
            {
                // An inline image of a tiling pattern or a glyph has no stable key (R4-8)
                return;
            }
            drawing.description = Helper::describe(document, image);
            drawing.dpi = QPointF(Helper::getDpi(drawing.description.pixelSize.width(), image.inches.x()),
                                  Helper::getDpi(drawing.description.pixelSize.height(), image.inches.y()));
            pageImages.drawings.push_back(std::move(drawing));
        };

        // The images of all layers (R4-4, R4-11)
        environment.processPageImages(PDFInteger(pageIndex), addDrawing, nullptr, operationControl);

        if (visibleLayers)
        {
            const PDFImageCompressor::DrawnImageCallback addVisible = [&pageImages](const PDFImageCompressor::DrawnImage& image)
            {
                pageImages.visibleKeys.insert(image.reference.isValid() ? Key::createReference(image.reference) : Key::createInline(image.inlineContentStream, image.inlineIndex));
            };
            environment.processPageImages(PDFInteger(pageIndex), addVisible, visibleLayers.get(), operationControl);
        }

        pageImages.isWalked = !PDFOperationControl::isOperationCanceled(operationControl);

        if (progress)
        {
            // A progress serves one thread at a time
            QMutexLocker locker(&progressMutex);
            progress->step();
        }
    };

    PDFIntegerRange<size_t> pageRange(0, pageCount);
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), processPage);

    if (progress && pageCount > 0)
    {
        progress->finish();
    }

    // The items in the order of their first drawing; the threads do not change it
    std::set<Key> visibleKeys;
    for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        Helper::PageImages& pageImages = pages[pageIndex];
        if (!pageImages.isWalked)
        {
            list.m_isComplete = false;
        }

        visibleKeys.insert(pageImages.visibleKeys.cbegin(), pageImages.visibleKeys.cend());
        for (Helper::Drawing& drawing : pageImages.drawings)
        {
            auto it = list.m_indices.find(drawing.key);
            if (it == list.m_indices.end())
            {
                Item item = std::move(drawing.description);
                item.key = drawing.key;
                item.kind = drawing.key.isInline() ? Kind::Inline : Kind::XObject;
                it = list.m_indices.emplace(drawing.key, list.m_items.size()).first;
                list.m_items.push_back(std::move(item));
            }

            Item& item = list.m_items[it->second];
            if (item.pageIndices.empty() || item.pageIndices.back() != PDFInteger(pageIndex))
            {
                item.pageIndices.push_back(PDFInteger(pageIndex));
            }
            item.minimalDpi = QPointF(Helper::getMinimumDpi(item.minimalDpi.x(), drawing.dpi.x()), Helper::getMinimumDpi(item.minimalDpi.y(), drawing.dpi.y()));
        }
    }

    // B62: a mask, which is also drawn as an image (or shared), is no independent item to change
    std::set<PDFObjectReference> maskReferences;
    for (const Item& item : list.m_items)
    {
        if (item.softMaskReference.isValid())
        {
            maskReferences.insert(item.softMaskReference);
        }
        if (item.maskReference.isValid())
        {
            maskReferences.insert(item.maskReference);
        }
    }

    // R4-4: the appearances of the annotations are never changed - an image drawn by the page and
    // by an appearance too is kept (a change in place would change the appearance)
    const std::set<PDFObjectReference> appearanceImages = collectAppearanceImages(document);
    for (Item& item : list.m_items)
    {
        item.isUsedAsMask = !item.key.isInline() && maskReferences.count(item.key.reference) > 0;
        item.isInHiddenLayer = visibleLayers && !visibleKeys.count(item.key);
        item.isInAppearance = !item.key.isInline() && appearanceImages.count(item.key.reference) > 0;
    }

    return list;
}

std::set<PDFObjectReference> PDFDocumentImageList::collectAppearanceImages(const PDFDocument* document)
{
    using Helper = PDFDocumentImageListHelper;

    std::set<PDFObjectReference> images;
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!catalog)
    {
        return images;
    }

    // One walk for the whole document: a form or an image shared by more appearances (or a form,
    // whose resources lead back to it) is read once
    PDFObjectWalk walk;
    for (size_t pageIndex = 0; pageIndex < catalog->getPageCount(); ++pageIndex)
    {
        const PDFObject& pageObject = document->getObjectByReference(catalog->getPage(pageIndex)->getPageReference());
        if (!pageObject.isDictionary())
        {
            continue;
        }

        const PDFObject& annotations = document->getObject(pageObject.getDictionary()->get("Annots"));
        if (!annotations.isArray())
        {
            continue;
        }

        for (size_t i = 0; i < annotations.getArray()->getCount(); ++i)
        {
            const PDFObject& annotation = annotations.getArray()->getItem(i);
            if (!walk.enter(annotation, 0))
            {
                continue;
            }
            const PDFObject& annotationObject = document->getObject(annotation);
            if (!annotationObject.isDictionary())
            {
                continue;
            }

            // 12.5.5, Table 170: /N, /R, /D - a stream, or a dictionary of the streams of the states
            const PDFObject& appearance = annotationObject.getDictionary()->get("AP");
            if (!walk.enter(appearance, 1))
            {
                continue;
            }
            const PDFObject& appearanceObject = document->getObject(appearance);
            if (!appearanceObject.isDictionary())
            {
                continue;
            }

            for (const char* kind : { "N", "R", "D" })
            {
                const PDFObject& entry = appearanceObject.getDictionary()->get(kind);
                const PDFObject& entryObject = document->getObject(entry);
                if (entryObject.isStream())
                {
                    Helper::collectAppearanceXObject(document, entry, 2, &walk, &images);
                }
                else if (entryObject.isDictionary() && walk.enter(entry, 2))
                {
                    const PDFDictionary* states = entryObject.getDictionary();
                    for (size_t state = 0; state < states->getCount(); ++state)
                    {
                        Helper::collectAppearanceXObject(document, states->getValue(state), 3, &walk, &images);
                    }
                }
            }
        }
    }
    return images;
}

const PDFDocumentImageList::Item* PDFDocumentImageList::findItem(const Key& key) const
{
    const int index = indexOf(key);
    return index >= 0 ? &m_items[size_t(index)] : nullptr;
}

int PDFDocumentImageList::indexOf(const Key& key) const
{
    auto it = m_indices.find(key);
    return it != m_indices.end() ? int(it->second) : -1;
}

qint64 PDFDocumentImageList::getTotalBytes() const
{
    qint64 total = 0;
    for (const Item& item : m_items)
    {
        total += item.streamBytes + item.maskBytes;
    }
    return total;
}

PDFDocumentImageList::ImageColor PDFDocumentImageList::getImageColor(const PDFDocument* document, const PDFDictionary* imageDictionary, const PDFObject& colorSpace)
{
    if (!document || !imageDictionary)
    {
        return ImageColor::Color;
    }

    // 8.9.5, Table 87: a stencil mask has one bit and no color space. An image of one bit per
    // component is bitonal only in a gray space of one component, or in a palette of two gray
    // entries (owner 14. 11.); 1 bit of DeviceRGB or of a red and blue palette is a color image.
    PDFDocumentDataLoaderDecorator loader(document);
    if (loader.readBooleanFromDictionary(imageDictionary, "ImageMask", false))
    {
        return ImageColor::Bitonal;
    }
    if (loader.readIntegerFromDictionary(imageDictionary, "BitsPerComponent", 0) == 1)
    {
        return PDFDocumentImageListHelper::isBitonalSpaceOfOneBit(document, colorSpace) ? ImageColor::Bitonal : ImageColor::Color;
    }

    return PDFDocumentImageListHelper::isGrayColorSpace(document, colorSpace, 0) ? ImageColor::Gray : ImageColor::Color;
}

PDFDocumentImageList::ImageColor PDFDocumentImageList::getImageColor(const PDFDocument* document, const PDFDictionary* imageDictionary)
{
    if (!document || !imageDictionary)
    {
        return ImageColor::Color;
    }
    return getImageColor(document, imageDictionary, document->getObject(imageDictionary->get("ColorSpace")));
}

bool PDFDocumentImageList::isColorImage(const PDFDocument* document, const PDFDictionary* imageDictionary, const PDFObject& colorSpace)
{
    return getImageColor(document, imageDictionary, colorSpace) == ImageColor::Color;
}

bool PDFDocumentImageList::isColorImage(const PDFDocument* document, const PDFDictionary* imageDictionary)
{
    return getImageColor(document, imageDictionary) == ImageColor::Color;
}

}   // namespace pdf
