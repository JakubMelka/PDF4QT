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

#include "pdfdocumentsizebreakdown.h"

#include "pdfcatalog.h"
#include "pdfdocumentwriter.h"
#include "pdfexecutionpolicy.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfutils.h"

#include <atomic>
#include <set>

namespace pdf
{

/// Helpers of the breakdown of the size of a document
class PDFDocumentSizeBreakdownHelper
{
public:
    PDFDocumentSizeBreakdownHelper() = delete;

    /// Row of an object
    enum class Kind : uint8_t
    {
        Other,
        Image,
        Font,
        PageContent
    };

    /// Marks a reference of the object list
    static void mark(std::vector<Kind>& kinds, const PDFObject& object, Kind kind)
    {
        if (object.isReference())
        {
            const PDFInteger number = object.getReference().objectNumber;
            if (number >= 0 && size_t(number) < kinds.size())
            {
                kinds[size_t(number)] = kind;
            }
        }
    }

    /// Marks the content streams of the pages (a stream or an array of streams, ISO 32000-2 Table 31)
    static void markPageContents(const PDFDocument* document, std::vector<Kind>& kinds)
    {
        const PDFCatalog* catalog = document->getCatalog();
        const size_t pageCount = catalog ? catalog->getPageCount() : 0;
        for (size_t i = 0; i < pageCount; ++i)
        {
            const PDFPage* page = catalog->getPage(i);
            const PDFDictionary* pageDictionary = page ? document->getDictionaryFromObject(document->getObjectByReference(page->getPageReference())) : nullptr;
            if (!pageDictionary)
            {
                continue;
            }

            const PDFObject& contents = pageDictionary->get("Contents");
            mark(kinds, contents, Kind::PageContent);

            const PDFObject& resolvedContents = document->getObject(contents);
            if (resolvedContents.isArray())
            {
                const PDFArray* array = resolvedContents.getArray();
                for (size_t item = 0, count = array->getCount(); item < count; ++item)
                {
                    mark(kinds, array->getItem(item), Kind::PageContent);
                }
            }
        }
    }
};

PDFDocumentSizeBreakdown PDFDocumentSizeBreakdown::create(const PDFDocument* document, qint64 fileSize, const PDFOperationControl* control)
{
    using Helper = PDFDocumentSizeBreakdownHelper;
    using Kind = Helper::Kind;

    PDFDocumentSizeBreakdown breakdown;
    if (!document)
    {
        return breakdown;
    }

    const PDFObjectStorage& storage = document->getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
    PDFDocumentDataLoaderDecorator loader(document);

    // What the objects are by themselves
    std::vector<Kind> kinds(objects.size(), Kind::Other);
    std::vector<PDFObject> fontRoots;
    std::set<PDFObjectReference> masks;
    std::vector<PDFInteger> imageIndices;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        const PDFObject& object = objects[i].object;
        const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : (object.isDictionary() ? object.getDictionary() : nullptr);
        if (!dictionary)
        {
            continue;
        }

        if (object.isStream())
        {
            const QByteArray subtype = loader.readNameFromDictionary(dictionary, "Subtype");
            if (subtype == "Image")
            {
                kinds[i] = Kind::Image;
                imageIndices.push_back(PDFInteger(i));

                for (const char* key : { "SMask", "Mask" })
                {
                    const PDFObject& mask = dictionary->get(key);
                    if (mask.isReference())
                    {
                        masks.insert(mask.getReference());
                    }
                }
                continue;
            }
            if (subtype == "Form")
            {
                kinds[i] = Kind::PageContent;
                continue;
            }
        }
        else
        {
            const QByteArray type = loader.readNameFromDictionary(dictionary, "Type");
            if (type == "Font" || type == "FontDescriptor")
            {
                fontRoots.push_back(PDFObject::createReference(PDFObjectReference(PDFInteger(i), objects[i].generation)));
            }
        }
    }

    Helper::markPageContents(document, kinds);

    // Everything else reachable from the fonts: their programs, encodings and CMaps
    for (const PDFObjectReference& reference : PDFObjectUtils::getReferences(fontRoots, storage))
    {
        if (reference.objectNumber >= 0 && size_t(reference.objectNumber) < kinds.size() && kinds[size_t(reference.objectNumber)] == Kind::Other)
        {
            kinds[size_t(reference.objectNumber)] = Kind::Font;
        }
    }

    // The sizes
    std::atomic<qint64> images = 0;
    std::atomic<qint64> fonts = 0;
    std::atomic<qint64> pageContent = 0;
    std::atomic<qint64> other = 0;
    PDFIntegerRange<size_t> range(0, objects.size());
    auto addSize = [&](size_t index)
    {
        if (objects[index].object.isNull() || PDFOperationControl::isOperationCancelled(control))
        {
            return;
        }

        const qint64 size = PDFDocumentWriter::getObjectSize(document, PDFObjectReference(PDFInteger(index), objects[index].generation));
        switch (kinds[index])
        {
            case Kind::Image:
                images += size;
                break;
            case Kind::Font:
                fonts += size;
                break;
            case Kind::PageContent:
                pageContent += size;
                break;
            case Kind::Other:
                other += size;
                break;
        }
    };
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, range.begin(), range.end(), addSize);

    breakdown.images = images.load();
    breakdown.fonts = fonts.load();
    breakdown.pageContent = pageContent.load();
    breakdown.structure = fileSize > 0 ? qMax(qint64(0), fileSize - breakdown.images - breakdown.fonts - breakdown.pageContent) : other.load();

    for (const PDFInteger index : imageIndices)
    {
        if (!masks.count(PDFObjectReference(index, objects[size_t(index)].generation)))
        {
            ++breakdown.imageCount;
        }
    }
    return breakdown;
}

QJsonObject PDFDocumentSizeBreakdown::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("images"), images);
    object.insert(QStringLiteral("fonts"), fonts);
    object.insert(QStringLiteral("pageContent"), pageContent);
    object.insert(QStringLiteral("structure"), structure);
    object.insert(QStringLiteral("imageCount"), imageCount);
    return object;
}

PDFDocumentSizeBreakdown PDFDocumentSizeBreakdown::fromJson(const QJsonObject& object)
{
    PDFDocumentSizeBreakdown breakdown;
    breakdown.images = object.value(QStringLiteral("images")).toInteger();
    breakdown.fonts = object.value(QStringLiteral("fonts")).toInteger();
    breakdown.pageContent = object.value(QStringLiteral("pageContent")).toInteger();
    breakdown.structure = object.value(QStringLiteral("structure")).toInteger();
    breakdown.imageCount = object.value(QStringLiteral("imageCount")).toInteger();
    return breakdown;
}

std::vector<PDFDocumentToolFact> PDFDocumentSizeBreakdown::createChangeRows(const PDFDocumentSizeBreakdown& before, const PDFDocumentSizeBreakdown& after)
{
    std::vector<PDFDocumentToolFact> rows;
    rows.push_back({ QStringLiteral("images"), tr("Images (%L1)").arg(before.imageCount), PDFDocumentToolFormat::formatSizeChange(before.images, after.images) });
    rows.push_back({ QStringLiteral("fonts"), tr("Fonts"), PDFDocumentToolFormat::formatSizeChange(before.fonts, after.fonts) });
    rows.push_back({ QStringLiteral("pageContent"), tr("Page content"), PDFDocumentToolFormat::formatSizeChange(before.pageContent, after.pageContent) });
    rows.push_back({ QStringLiteral("structure"), tr("Structure"), PDFDocumentToolFormat::formatSizeChange(before.structure, after.structure) });
    return rows;
}

}   // namespace pdf
