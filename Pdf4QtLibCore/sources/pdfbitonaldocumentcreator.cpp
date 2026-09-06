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

#include "pdfbitonaldocumentcreator.h"
#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfnametreeloader.h"
#include "pdfnumbertreeloader.h"
#include "pdfoptimizer.h"
#include "pdfpage.h"
#include "pdfprogress.h"
#include "pdfstreamfilters.h"
#include "pdfutils.h"

#include <QPainter>
#include <QScopeGuard>

#include <map>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

/// Entries of the original image dictionary, which are still valid for the converted
/// bitonal image. Entries describing the image samples (size, color space, filters)
/// and the transparency (soft masks) are always replaced by the converted ones.
static constexpr const char* PRESERVED_IMAGE_DICTIONARY_KEYS[] =
{
    "Intent",
    "Interpolate",
    "OC",
    "Metadata",
    "StructParent",
    "AF",
    "Measure",
    "PtData"
};

/// Composites the rasterized page onto the white background. Areas of the page,
/// which are not painted at all, are transparent in the rasterized image, but
/// they represent a blank paper, so they must become white.
static QImage compositePageImageOntoWhite(const QImage& image)
{
    if (image.isNull())
    {
        return image;
    }

    QImage result(image.size(), QImage::Format_RGB32);

    if (result.isNull())
    {
        // The image cannot be allocated
        return QImage();
    }

    result.fill(Qt::white);

    QPainter painter(&result);
    painter.drawImage(0, 0, image);
    painter.end();

    return result;
}

PDFBitonalDocumentCreator::PDFBitonalDocumentCreator(const PDFDocument* document,
                                                     PDFRasterizerPool* rasterizerPool,
                                                     PDFProgress* progress) :
    m_document(document),
    m_rasterizerPool(rasterizerPool),
    m_progress(progress)
{
    Q_ASSERT(m_document);
}

PDFRenderer::Features PDFBitonalDocumentCreator::getPageRasterizationFeatures()
{
    return PDFRenderer::Features(PDFRenderer::Antialiasing |
                                 PDFRenderer::TextAntialiasing |
                                 PDFRenderer::SmoothImages |
                                 PDFRenderer::ClipToCropBox);
}

bool PDFBitonalDocumentCreator::createBitonalDocument(const Settings& settings)
{
    m_bitonalDocument = PDFDocument();
    m_convertedItemCount = 0;
    m_failedItemCount = 0;

    try
    {
        PDFDocumentBuilder builder(m_document);
        bool isConverted = false;

        switch (settings.conversionSource)
        {
            case ConversionSource::Images:
                isConverted = createBitonalDocumentFromImages(builder, settings);
                break;

            case ConversionSource::Pages:
                isConverted = createBitonalDocumentFromPages(builder, settings);
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        if (!isConverted)
        {
            // Nothing has been converted
            return false;
        }

        PDFDocument builtDocument = builder.build();

        // Images and content streams, which have been replaced, are not referenced
        // by the document anymore. They must be removed, otherwise the converted
        // document would be even larger than the original one.
        PDFOptimizer optimizer(PDFOptimizer::RemoveUnusedObjects, nullptr);
        optimizer.setDocument(&builtDocument);
        optimizer.optimize();

        m_bitonalDocument = PDFDocument(optimizer.takeStorage(), builtDocument.getInfo()->version, QByteArray());
        return true;
    }
    catch (const PDFException&)
    {
        m_bitonalDocument = PDFDocument();
        return false;
    }
}

bool PDFBitonalDocumentCreator::createBitonalDocumentFromImages(PDFDocumentBuilder& builder, const Settings& settings)
{
    // The settings are a public API, so they can contain the same image more than
    // once. The mode of the last occurrence wins - including the mode, which leaves
    // the image untouched, so a later item can cancel an earlier request. Only after
    // the duplicates are resolved, the untouched images are left out.
    std::map<PDFObjectReference, ItemMode> imageModes;
    std::vector<PDFObjectReference> imageOrder;

    for (const ItemInfo& item : settings.items)
    {
        if (!item.imageReference.isValid())
        {
            continue;
        }

        if (imageModes.insert_or_assign(item.imageReference, item.mode).second)
        {
            imageOrder.push_back(item.imageReference);
        }
    }

    std::vector<ItemInfo> itemsToBeConverted;

    for (const PDFObjectReference reference : imageOrder)
    {
        ItemInfo item;
        item.imageReference = reference;
        item.mode = imageModes.at(reference);

        if (item.isContentReplaced())
        {
            itemsToBeConverted.push_back(item);
        }
    }

    // Do we have something to be converted?
    if (itemsToBeConverted.empty())
    {
        return false;
    }

    startProgress(itemsToBeConverted.size(), PDFTranslationContext::tr("Converting images..."));

    // The progress must be finished even when an exception escapes from this function
    auto progressGuard = qScopeGuard([this]() { finishProgress(); });

    bool isConverted = false;

    for (const ItemInfo& item : itemsToBeConverted)
    {
        const PDFObjectReference reference = item.imageReference;
        QImage image = getDecodedImage(reference, nullptr);

        if (image.isNull())
        {
            // Image cannot be decoded, so it is left in the document as it is
            ++m_failedItemCount;
            stepProgress();
            continue;
        }

        QImage alphaMask;
        QImage bitonalImage;

        if (item.isFilled())
        {
            // Only the color samples are replaced by the solid fill - the transparency
            // of the original image is preserved. Filling a masked text layer of a
            // two-layer scan without its mask would cover the whole page.
            std::optional<QImage> mask = PDFImageConversion::createAlphaMask(image);

            if (!mask)
            {
                // The mask cannot be created, so the transparency would be lost - the
                // image is left as it is
                ++m_failedItemCount;
                stepProgress();
                continue;
            }

            alphaMask = std::move(*mask);
            bitonalImage = createFillImage(alphaMask.isNull() ? QSize(1, 1) : alphaMask.size(), item.isFilledByBlack());
        }
        else
        {
            bitonalImage = convertImageToBitonal(image, settings.conversionMethod, settings.manualThreshold, &alphaMask, nullptr);
        }

        PDFObject imageObject = createBitonalImageObject(bitonalImage);

        if (!imageObject.isNull())
        {
            PDFDictionary dictionary = *imageObject.getStream()->getDictionary();
            QByteArray content = *imageObject.getStream()->getContent();

            // Transfer the entries of the original image, which are not related
            // to the image samples, into the converted image.
            if (const PDFDictionary* originalDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference)))
            {
                for (const char* key : PRESERVED_IMAGE_DICTIONARY_KEYS)
                {
                    if (originalDictionary->hasKey(key))
                    {
                        dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject(originalDictionary->get(key)));
                    }
                }
            }

            // The original image can be transparent - it can have a soft mask, a stencil
            // mask, a color key mask, or an alpha channel stored directly in the image
            // data. All these variants are decoded into the alpha channel of the decoded
            // image, so a single soft mask created from that alpha channel replaces them.
            if (!alphaMask.isNull())
            {
                PDFObject softMaskObject = createBitonalImageObject(alphaMask);

                if (!softMaskObject.isNull())
                {
                    PDFObjectReference softMaskReference = builder.addObject(std::move(softMaskObject));
                    dictionary.setEntry(PDFInplaceOrMemoryString("SMask"), PDFObject::createReference(softMaskReference));
                }
            }

            builder.setObject(reference, PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), std::move(content))));
            isConverted = true;
            ++m_convertedItemCount;
        }
        else
        {
            ++m_failedItemCount;
        }

        stepProgress();
    }

    return isConverted;
}

bool PDFBitonalDocumentCreator::createBitonalDocumentFromPages(PDFDocumentBuilder& builder, const Settings& settings)
{
    const PDFCatalog* catalog = m_document->getCatalog();
    const size_t pageCount = catalog->getPageCount();

    // The settings are a public API, so they can contain an index of a page, which does
    // not exist, or the same page more than once. Both must be resolved before anything
    // is rasterized - a duplicated page in the algorithm mode would be rendered by two
    // tasks writing into the same slot at the same time, which is a data race, and an
    // index out of the range would access the vector of the images outside its bounds.
    // When a page is requested more than once, the mode of its last occurrence wins -
    // including the mode, which leaves the page untouched, so a later item can cancel
    // an earlier request. Only after the duplicates are resolved, the untouched pages
    // are left out.
    std::map<PDFInteger, ItemMode> pageModes;

    for (const ItemInfo& item : settings.items)
    {
        if (item.pageIndex >= 0 && size_t(item.pageIndex) < pageCount)
        {
            pageModes[item.pageIndex] = item.mode;
        }
    }

    std::erase_if(pageModes, [](const auto& entry) { return entry.second == ItemMode::Original; });

    // Do we have something to be converted?
    if (pageModes.empty())
    {
        return false;
    }

    // Only the pages converted by the algorithm have to be rasterized - a filled page
    // is built from a single sample, so it costs nothing.
    std::vector<PDFInteger> pageIndices;
    std::vector<PDFInteger> rasterizedPageIndices;
    pageIndices.reserve(pageModes.size());

    for (const auto& [pageIndex, mode] : pageModes)
    {
        pageIndices.push_back(pageIndex);

        if (mode == ItemMode::Algorithm)
        {
            rasterizedPageIndices.push_back(pageIndex);
        }
    }

    if (!rasterizedPageIndices.empty() && !m_rasterizerPool)
    {
        // Pages cannot be rasterized without the pool
        return false;
    }

    // Rasterizing is the only expensive part of the conversion, so the progress counts
    // the rasterized pages. At least one step is needed even when all pages are only
    // filled, so the progress does not divide by a zero page count.
    startProgress(qMax<size_t>(rasterizedPageIndices.size(), 1), PDFTranslationContext::tr("Converting pages..."));

    // The progress must be finished even when an exception escapes from this function
    auto progressGuard = qScopeGuard([this]() { finishProgress(); });

    bool isConverted = false;
    const int dpiResolution = settings.dpiResolution;

    // Pages are rasterized and converted in parallel, but the converted images are
    // stored as encoded image streams only - the rasterized images are large and we
    // do not want to keep all of them in the memory at once.
    std::vector<PDFObject> imageObjects(pageCount);

    auto pageSizeGetter = [dpiResolution](const PDFPage* page) { return getPageImageSize(page, dpiResolution); };
    auto pageImageProcessor = [this, &imageObjects, &settings](PDFInteger pageIndex, QImage image)
    {
        QImage bitonalImage = convertImageToBitonal(image, settings.conversionMethod, settings.manualThreshold, nullptr, nullptr);
        imageObjects[size_t(pageIndex)] = createBitonalImageObject(bitonalImage);
        stepProgress();
    };

    renderPages(rasterizedPageIndices, pageSizeGetter, pageImageProcessor, nullptr);

    // Pages, whose content has really been replaced. A page, which could not be
    // rendered, keeps its original content, so it also keeps its marked content and
    // it must be counted as not converted.
    std::set<PDFObjectReference> convertedPages;

    for (const PDFInteger pageIndex : pageIndices)
    {
        const ItemMode mode = pageModes.at(pageIndex);
        PDFObject imageObject;

        if (mode == ItemMode::Algorithm)
        {
            imageObject = std::move(imageObjects[size_t(pageIndex)]);
        }
        else
        {
            // Page is covered by a solid area, which does not need any rasterization
            imageObject = createBitonalImageObject(createFillImage(QSize(1, 1), mode == ItemMode::FillBlack));
        }

        const PDFPage* page = catalog->getPage(size_t(pageIndex));

        if (imageObject.isNull() || !page)
        {
            // The page could not be rendered or encoded, so it stays as it is
            ++m_failedItemCount;
            continue;
        }

        const QRectF mediaBox = page->getMediaBox();
        if (mediaBox.isEmpty())
        {
            ++m_failedItemCount;
            continue;
        }

        const PDFObjectReference imageReference = builder.addObject(std::move(imageObject));

        // Image is placed onto the whole media box of the page. The media box is
        // expressed in the coordinate system of the page, in which the y axis points
        // upwards, so the lower left corner of the box is (left(), top()).
        QByteArray contentStream = QString("q %1 0 0 %2 %3 %4 cm /BitonalImage Do Q")
                                       .arg(mediaBox.width(), 0, 'f', 6)
                                       .arg(mediaBox.height(), 0, 'f', 6)
                                       .arg(mediaBox.left(), 0, 'f', 6)
                                       .arg(mediaBox.top(), 0, 'f', 6).toLatin1();
        QByteArray compressedContentStream = PDFFlateDecodeFilter::compress(contentStream);

        PDFDictionary contentStreamDictionary;
        contentStreamDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        contentStreamDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedContentStream.size()));

        const PDFObjectReference contentStreamReference = builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(contentStreamDictionary), std::move(compressedContentStream))));

        PDFDictionary xobjectDictionary;
        xobjectDictionary.setEntry(PDFInplaceOrMemoryString("BitonalImage"), PDFObject::createReference(imageReference));

        PDFArray procSetArray;
        procSetArray.appendItem(PDFObject::createName("PDF"));
        procSetArray.appendItem(PDFObject::createName("ImageB"));

        PDFDictionary resourcesDictionary;
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("XObject"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(xobjectDictionary))));
        resourcesDictionary.setEntry(PDFInplaceOrMemoryString("ProcSet"), PDFObject::createArray(std::make_shared<PDFArray>(std::move(procSetArray))));

        const PDFObjectReference pageReference = page->getPageReference();
        const PDFDictionary* originalPageDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(pageReference));

        if (!originalPageDictionary)
        {
            ++m_failedItemCount;
            continue;
        }

        // Everything except the page content is preserved - the page keeps its size,
        // rotation, annotations and other properties.
        PDFDictionary pageDictionary = *originalPageDictionary;
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contentStreamReference));
        pageDictionary.setEntry(PDFInplaceOrMemoryString("Resources"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(resourcesDictionary))));

        // The marked content of the page is gone, so the page must not be a part
        // of the structure tree anymore.
        pageDictionary.removeEntry("StructParents");

        // The embedded thumbnail shows the original, colored content of the page.
        // Keeping it would both display a wrong preview in the viewer and prevent
        // the removal of the unused objects it references.
        pageDictionary.removeEntry("Thumb");

        builder.setObject(pageReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(pageDictionary))));
        isConverted = true;
        convertedPages.insert(pageReference);
        ++m_convertedItemCount;
    }

    if (convertedPages.size() == pageCount)
    {
        // Every page of the document has really got a new content stream, so no page
        // is tagged anymore and the whole structure tree can be removed
        removeStructureTree(builder);
    }
    else if (!convertedPages.empty())
    {
        // The other pages keep their marked content, so the structure tree must
        // survive, but it must not refer to the content, which is gone
        pruneStructureTree(builder, convertedPages);
    }

    return isConverted;
}

void PDFBitonalDocumentCreator::removeStructureTree(PDFDocumentBuilder& builder) const
{
    const PDFObjectReference catalogReference = builder.getCatalogReference();

    if (const PDFDictionary* originalCatalogDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(catalogReference)))
    {
        if (!originalCatalogDictionary->hasKey("StructTreeRoot") && !originalCatalogDictionary->hasKey("MarkInfo"))
        {
            return;
        }

        PDFDictionary catalogDictionary = *originalCatalogDictionary;
        catalogDictionary.removeEntry("StructTreeRoot");
        catalogDictionary.removeEntry("MarkInfo");
        builder.setObject(catalogReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(catalogDictionary))));
    }
}

/// Removes the references to the content of the converted pages from the structure
/// tree of a document. The content of a converted page is a single image, so the
/// marked content sequences of the page do not exist anymore and the structure
/// elements must not refer to them - a viewer would look up the marked content
/// identifiers in a content stream, which does not contain them, and an extraction
/// of the text would use the actual text of an element, whose content is gone.
///
/// The pruning walks the tree from its root. A marked content reference (an integer
/// identifier or a MCR dictionary) lying on a converted page is removed, an object
/// reference lying on a converted page is removed unless it refers to an annotation
/// of the page (annotations are kept alive by the conversion), and an element, which
/// has lost all its kids, is removed as well. The parent tree loses the entries of
/// the converted pages and the entries pointing to the removed elements, the ID tree
/// loses the removed elements and the Ref entries of the kept elements do not point
/// to the removed elements anymore.
class PDFStructureTreePruner
{
public:
    explicit PDFStructureTreePruner(const PDFDocument* document,
                                    PDFDocumentBuilder* builder,
                                    const std::set<PDFObjectReference>& convertedPages) :
        m_document(document),
        m_builder(builder),
        m_convertedPages(convertedPages),
        m_loader(document)
    {

    }

    /// Prunes the tree. Returns false, if no element remains in the tree, so the
    /// tree should be removed altogether.
    bool prune();

private:
    /// Result of the pruning of an element dictionary
    struct ElementResult
    {
        bool isKept = true;
        bool isChanged = false;
        PDFDictionary dictionary;
    };

    /// Result of the pruning of the kids of an element
    struct KidsResult
    {
        bool isChanged = false;
        PDFObject kids;
    };

    KidsResult pruneKids(const PDFObject& kids, PDFObjectReference page);
    std::optional<PDFObject> pruneKid(const PDFObject& kid, PDFObjectReference page);
    ElementResult pruneElementDictionary(const PDFDictionary* dictionary, PDFObjectReference inheritedPage);
    bool pruneElement(PDFObjectReference reference, PDFObjectReference inheritedPage);
    PDFObject rebuildParentTree(const PDFObject& parentTree, const std::set<PDFInteger>& removedKeys);
    PDFObject rebuildIdTree(const PDFObject& idTree);
    void pruneElementReferences();

    bool isPageConverted(PDFObjectReference page) const { return m_convertedPages.count(page) > 0; }
    bool isElementRemoved(const PDFObject& object) const { return object.isReference() && m_removedElements.count(object.getReference()) > 0; }
    bool isAnnotationOfPage(PDFObjectReference page, PDFObjectReference object) const;

    /// Returns the effective page of a structure item - its own page, or the page
    /// inherited from its parent element, when it has none
    PDFObjectReference getEffectivePage(const PDFDictionary* dictionary, PDFObjectReference inheritedPage) const;

    /// Writes the object into the builder, either as the indirect object or as
    /// a direct entry of the parent dictionary
    static void setObject(PDFDocumentBuilder* builder, const PDFObject& original, PDFDictionary& parentDictionary, const char* key, PDFObject object);

    const PDFDocument* m_document;
    PDFDocumentBuilder* m_builder;
    const std::set<PDFObjectReference>& m_convertedPages;
    PDFDocumentDataLoaderDecorator m_loader;

    /// Elements, which have already been visited, and whether they have been kept
    std::map<PDFObjectReference, bool> m_visitedElements;

    /// Elements, which have been removed from the tree
    std::set<PDFObjectReference> m_removedElements;

    /// Kept elements, which refer to other elements by the Ref entry
    std::vector<PDFObjectReference> m_elementsWithReferences;
};

bool PDFStructureTreePruner::prune()
{
    const PDFObjectReference catalogReference = m_builder->getCatalogReference();
    const PDFDictionary* catalogDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(catalogReference));

    if (!catalogDictionary)
    {
        return true;
    }

    const PDFObject rootObject = catalogDictionary->get("StructTreeRoot");
    const PDFDictionary* rootDictionary = m_document->getDictionaryFromObject(rootObject);

    if (!rootDictionary)
    {
        return true;
    }

    // The keys of the parent tree, which belong to the converted pages, are gone
    // together with the marked content of the pages
    std::set<PDFInteger> removedParentTreeKeys;

    for (const PDFObjectReference pageReference : m_convertedPages)
    {
        if (const PDFDictionary* pageDictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(pageReference)))
        {
            const PDFObject structParents = m_document->getObject(pageDictionary->get("StructParents"));

            if (structParents.isInt())
            {
                removedParentTreeKeys.insert(structParents.getInteger());
            }
        }
    }

    const PDFObject originalKids = rootDictionary->get("K");
    KidsResult kids = pruneKids(originalKids, PDFObjectReference());

    if (kids.isChanged && kids.kids.isNull())
    {
        // Every element of the tree has been removed
        return false;
    }

    pruneElementReferences();

    PDFDictionary newRootDictionary = *rootDictionary;
    bool isRootChanged = false;

    if (kids.isChanged)
    {
        newRootDictionary.setEntry(PDFInplaceOrMemoryString("K"), std::move(kids.kids));
        isRootChanged = true;
    }

    if (rootDictionary->hasKey("ParentTree"))
    {
        const PDFObject originalParentTree = rootDictionary->get("ParentTree");
        PDFObject parentTree = rebuildParentTree(originalParentTree, removedParentTreeKeys);

        if (!parentTree.isNull())
        {
            setObject(m_builder, originalParentTree, newRootDictionary, "ParentTree", std::move(parentTree));
            isRootChanged = true;
        }
    }

    if (rootDictionary->hasKey("IDTree"))
    {
        const PDFObject originalIdTree = rootDictionary->get("IDTree");
        PDFObject idTree = rebuildIdTree(originalIdTree);

        if (!idTree.isNull())
        {
            setObject(m_builder, originalIdTree, newRootDictionary, "IDTree", std::move(idTree));
            isRootChanged = true;
        }
    }

    if (isRootChanged)
    {
        if (rootObject.isReference())
        {
            m_builder->setObject(rootObject.getReference(), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(newRootDictionary))));
        }
        else
        {
            PDFDictionary newCatalogDictionary = *catalogDictionary;
            newCatalogDictionary.setEntry(PDFInplaceOrMemoryString("StructTreeRoot"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(newRootDictionary))));
            m_builder->setObject(catalogReference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(newCatalogDictionary))));
        }
    }

    return true;
}

PDFStructureTreePruner::KidsResult PDFStructureTreePruner::pruneKids(const PDFObject& kids, PDFObjectReference page)
{
    KidsResult result;
    result.kids = kids;

    const PDFObject dereferencedKids = m_document->getObject(kids);

    if (dereferencedKids.isArray())
    {
        PDFArray newKids;
        const PDFArray* kidsArray = dereferencedKids.getArray();

        for (size_t i = 0, count = kidsArray->getCount(); i < count; ++i)
        {
            const PDFObject& kid = kidsArray->getItem(i);
            std::optional<PDFObject> newKid = pruneKid(kid, page);

            if (!newKid)
            {
                result.isChanged = true;
                continue;
            }

            if (!(*newKid == kid))
            {
                result.isChanged = true;
            }

            newKids.appendItem(std::move(*newKid));
        }

        if (result.isChanged)
        {
            result.kids = newKids.getCount() > 0 ? PDFObject::createArray(std::make_shared<PDFArray>(std::move(newKids))) : PDFObject();
        }
    }
    else if (!kids.isNull())
    {
        std::optional<PDFObject> newKid = pruneKid(kids, page);

        if (!newKid)
        {
            result.isChanged = true;
            result.kids = PDFObject();
        }
        else if (!(*newKid == kids))
        {
            result.isChanged = true;
            result.kids = std::move(*newKid);
        }
    }

    return result;
}

std::optional<PDFObject> PDFStructureTreePruner::pruneKid(const PDFObject& kid, PDFObjectReference page)
{
    if (kid.isInt())
    {
        // A marked content identifier of the page of the element
        if (isPageConverted(page))
        {
            return std::nullopt;
        }

        return kid;
    }

    if (kid.isArray())
    {
        // A nested array of kids is not standard, but it is handled the same way
        KidsResult result = pruneKids(kid, page);

        if (result.isChanged && result.kids.isNull())
        {
            return std::nullopt;
        }

        return result.kids;
    }

    const PDFDictionary* dictionary = m_document->getDictionaryFromObject(kid);

    if (!dictionary)
    {
        // Something, which is not understood, is left as it is
        return kid;
    }

    const QByteArray type = m_loader.readNameFromDictionary(dictionary, "Type");

    if (type == "MCR")
    {
        // A marked content sequence of the page of the reference
        if (isPageConverted(getEffectivePage(dictionary, page)))
        {
            return std::nullopt;
        }

        return kid;
    }

    if (type == "OBJR")
    {
        // An object of the page - the annotations of the converted page are kept
        // alive by the conversion, everything else of the page is gone
        const PDFObjectReference objectPage = getEffectivePage(dictionary, page);

        if (isPageConverted(objectPage) && !isAnnotationOfPage(objectPage, m_loader.readReferenceFromDictionary(dictionary, "Obj")))
        {
            return std::nullopt;
        }

        return kid;
    }

    // A structure element
    if (kid.isReference())
    {
        if (!pruneElement(kid.getReference(), page))
        {
            return std::nullopt;
        }

        return kid;
    }

    ElementResult result = pruneElementDictionary(dictionary, page);

    if (!result.isKept)
    {
        return std::nullopt;
    }

    if (result.isChanged)
    {
        return PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(result.dictionary)));
    }

    return kid;
}

PDFStructureTreePruner::ElementResult PDFStructureTreePruner::pruneElementDictionary(const PDFDictionary* dictionary, PDFObjectReference inheritedPage)
{
    ElementResult result;

    const PDFObjectReference page = getEffectivePage(dictionary, inheritedPage);
    const PDFObject originalKids = dictionary->get("K");
    KidsResult kids = pruneKids(originalKids, page);

    if (!kids.isChanged)
    {
        return result;
    }

    if (kids.kids.isNull())
    {
        // The element has lost all its content
        result.isKept = false;
        return result;
    }

    result.isChanged = true;
    result.dictionary = *dictionary;
    result.dictionary.setEntry(PDFInplaceOrMemoryString("K"), std::move(kids.kids));
    return result;
}

bool PDFStructureTreePruner::pruneElement(PDFObjectReference reference, PDFObjectReference inheritedPage)
{
    auto it = m_visitedElements.find(reference);

    if (it != m_visitedElements.cend())
    {
        // The element has already been processed, or it is being processed - a damaged
        // document can contain a cycle, so an element being processed is kept
        return it->second;
    }

    m_visitedElements[reference] = true;

    const PDFDictionary* dictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference));

    if (!dictionary)
    {
        return true;
    }

    ElementResult result = pruneElementDictionary(dictionary, inheritedPage);

    if (!result.isKept)
    {
        m_visitedElements[reference] = false;
        m_removedElements.insert(reference);
        return false;
    }

    if (result.isChanged)
    {
        m_builder->setObject(reference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(result.dictionary))));
    }

    if (dictionary->hasKey("Ref"))
    {
        m_elementsWithReferences.push_back(reference);
    }

    return true;
}

PDFObject PDFStructureTreePruner::rebuildParentTree(const PDFObject& parentTree, const std::set<PDFInteger>& removedKeys)
{
    struct Entry
    {
        PDFInteger key = 0;
        PDFObject value;

        bool operator<(const Entry& other) const { return key < other.key; }

        static Entry parse(PDFInteger key, const PDFObjectStorage*, const PDFObject& object)
        {
            return Entry{ key, object };
        }
    };

    std::vector<Entry> entries = PDFNumberTreeLoader<Entry>::parse(&m_document->getStorage(), parentTree);
    bool isChanged = false;

    PDFArray numbers;

    for (Entry& entry : entries)
    {
        if (removedKeys.count(entry.key) > 0)
        {
            // The marked content of the page is gone
            isChanged = true;
            continue;
        }

        if (isElementRemoved(entry.value))
        {
            // The element, which the object belongs to, is gone
            isChanged = true;
            continue;
        }

        const PDFObject dereferencedValue = m_document->getObject(entry.value);

        if (dereferencedValue.isArray())
        {
            // The array maps the marked content identifiers of a page to the elements.
            // The page is not converted, so its elements are kept - unless the document
            // is inconsistent. A removed element is replaced by null, so the identifiers
            // of the other elements keep their positions.
            const PDFArray* array = dereferencedValue.getArray();
            PDFArray newArray;
            bool isArrayChanged = false;

            for (size_t i = 0, count = array->getCount(); i < count; ++i)
            {
                const PDFObject& item = array->getItem(i);

                if (isElementRemoved(item))
                {
                    newArray.appendItem(PDFObject());
                    isArrayChanged = true;
                }
                else
                {
                    newArray.appendItem(item);
                }
            }

            if (isArrayChanged)
            {
                PDFObject newArrayObject = PDFObject::createArray(std::make_shared<PDFArray>(std::move(newArray)));

                if (entry.value.isReference())
                {
                    m_builder->setObject(entry.value.getReference(), std::move(newArrayObject));
                }
                else
                {
                    entry.value = std::move(newArrayObject);
                    isChanged = true;
                }
            }
        }

        numbers.appendItem(PDFObject::createInteger(entry.key));
        numbers.appendItem(entry.value);
    }

    if (!isChanged)
    {
        return PDFObject();
    }

    // The tree is rebuilt as a single node - that is a valid number tree of any size
    PDFDictionary dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Nums"), PDFObject::createArray(std::make_shared<PDFArray>(std::move(numbers))));
    return PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(dictionary)));
}

PDFObject PDFStructureTreePruner::rebuildIdTree(const PDFObject& idTree)
{
    const std::map<QByteArray, PDFObject> entries = PDFNameTreeLoader<PDFObject>::parse(&m_document->getStorage(), idTree, [](const PDFObjectStorage*, const PDFObject& object) { return object; });
    bool isChanged = false;

    PDFArray names;

    for (const auto& [name, value] : entries)
    {
        if (isElementRemoved(value))
        {
            isChanged = true;
            continue;
        }

        names.appendItem(PDFObject::createString(name));
        names.appendItem(value);
    }

    if (!isChanged)
    {
        return PDFObject();
    }

    // The tree is rebuilt as a single node with the keys in the byte order, in which
    // the map keeps them
    PDFDictionary dictionary;
    dictionary.setEntry(PDFInplaceOrMemoryString("Names"), PDFObject::createArray(std::make_shared<PDFArray>(std::move(names))));
    return PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(dictionary)));
}

void PDFStructureTreePruner::pruneElementReferences()
{
    for (const PDFObjectReference reference : m_elementsWithReferences)
    {
        // The element can already be rewritten by the pruning of its kids, so the
        // current dictionary is taken from the builder
        const PDFObject elementObject = m_builder->getObjectByReference(reference);
        const PDFDictionary* dictionary = elementObject.isDictionary() ? elementObject.getDictionary() : nullptr;

        if (!dictionary)
        {
            continue;
        }

        const PDFObject references = m_document->getObject(dictionary->get("Ref"));

        if (!references.isArray())
        {
            continue;
        }

        PDFArray newReferences;
        bool isChanged = false;

        for (size_t i = 0, count = references.getArray()->getCount(); i < count; ++i)
        {
            const PDFObject& item = references.getArray()->getItem(i);

            if (isElementRemoved(item))
            {
                isChanged = true;
                continue;
            }

            newReferences.appendItem(item);
        }

        if (!isChanged)
        {
            continue;
        }

        PDFDictionary newDictionary = *dictionary;

        if (newReferences.getCount() > 0)
        {
            newDictionary.setEntry(PDFInplaceOrMemoryString("Ref"), PDFObject::createArray(std::make_shared<PDFArray>(std::move(newReferences))));
        }
        else
        {
            newDictionary.removeEntry("Ref");
        }

        m_builder->setObject(reference, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(newDictionary))));
    }
}

bool PDFStructureTreePruner::isAnnotationOfPage(PDFObjectReference page, PDFObjectReference object) const
{
    if (!object.isValid())
    {
        return false;
    }

    const PDFCatalog* catalog = m_document->getCatalog();
    const size_t pageIndex = catalog->getPageIndexFromPageReference(page);

    if (pageIndex >= catalog->getPageCount())
    {
        return false;
    }

    const PDFPage* pageObject = catalog->getPage(pageIndex);

    if (!pageObject)
    {
        return false;
    }

    const std::vector<PDFObjectReference>& annotations = pageObject->getAnnotations();
    return std::find(annotations.cbegin(), annotations.cend(), object) != annotations.cend();
}

PDFObjectReference PDFStructureTreePruner::getEffectivePage(const PDFDictionary* dictionary, PDFObjectReference inheritedPage) const
{
    const PDFObjectReference page = m_loader.readReferenceFromDictionary(dictionary, "Pg");
    return page.isValid() ? page : inheritedPage;
}

void PDFStructureTreePruner::setObject(PDFDocumentBuilder* builder, const PDFObject& original, PDFDictionary& parentDictionary, const char* key, PDFObject object)
{
    if (original.isReference())
    {
        // The original object is indirect, so it is replaced in place and the parent
        // dictionary keeps referring to it
        builder->setObject(original.getReference(), std::move(object));
    }
    else
    {
        parentDictionary.setEntry(PDFInplaceOrMemoryString(key), std::move(object));
    }
}

void PDFBitonalDocumentCreator::pruneStructureTree(PDFDocumentBuilder& builder, const std::set<PDFObjectReference>& convertedPages) const
{
    PDFStructureTreePruner pruner(m_document, &builder, convertedPages);

    if (!pruner.prune())
    {
        // Nothing has remained in the tree
        removeStructureTree(builder);
    }
}

void PDFBitonalDocumentCreator::traversePageImages(const PDFPage* page,
                                                   const std::function<void(PDFObjectReference, const PDFDictionary*)>& imageProcessor) const
{
    if (!page)
    {
        return;
    }

    PDFDocumentDataLoaderDecorator loader(m_document);

    // Resources are an inheritable attribute of the page tree, so they can be stored
    // in a parent node of the page - PDFPage resolves the inheritance for us. Images
    // can also be hidden inside a form XObject, which has resources of its own, so
    // the forms are entered as well.
    std::set<PDFObjectReference> visitedForms;
    std::vector<PDFObject> resourcesToBeProcessed;
    resourcesToBeProcessed.push_back(page->getResources());

    while (!resourcesToBeProcessed.empty())
    {
        const PDFObject resources = resourcesToBeProcessed.back();
        resourcesToBeProcessed.pop_back();

        const PDFDictionary* resourcesDictionary = m_document->getDictionaryFromObject(resources);
        if (!resourcesDictionary)
        {
            continue;
        }

        const PDFDictionary* xobjectDictionary = m_document->getDictionaryFromObject(resourcesDictionary->get("XObject"));
        if (!xobjectDictionary)
        {
            continue;
        }

        for (size_t index = 0, count = xobjectDictionary->getCount(); index < count; ++index)
        {
            const PDFObject& item = xobjectDictionary->getValue(index);

            // An XObject is always a stream, so it is always an indirect object
            if (!item.isReference())
            {
                continue;
            }

            const PDFObjectReference reference = item.getReference();
            const PDFDictionary* itemDictionary = m_document->getDictionaryFromObject(item);

            if (!itemDictionary)
            {
                continue;
            }

            const QByteArray subtype = loader.readNameFromDictionary(itemDictionary, "Subtype");

            if (subtype == "Image")
            {
                imageProcessor(reference, itemDictionary);
            }
            else if (subtype == "Form" && visitedForms.insert(reference).second)
            {
                // A damaged document can contain a form referencing itself, so every
                // form is entered only once.
                resourcesToBeProcessed.push_back(itemDictionary->get("Resources"));
            }
        }
    }
}

std::vector<PDFObjectReference> PDFBitonalDocumentCreator::getConvertibleImages() const
{
    std::vector<PDFObjectReference> references;
    std::set<PDFObjectReference> foundImages;
    PDFDocumentDataLoaderDecorator loader(m_document);

    // Images are collected in the page order, so the list, which a user interface
    // builds from them, follows the document. The same image can be used on several
    // pages, so it is reported only once.
    const PDFCatalog* catalog = m_document->getCatalog();
    for (size_t pageIndex = 0, pageCount = catalog->getPageCount(); pageIndex < pageCount; ++pageIndex)
    {
        traversePageImages(catalog->getPage(pageIndex), [&](PDFObjectReference reference, const PDFDictionary* imageDictionary)
        {
            if (loader.readBooleanFromDictionary(imageDictionary, "ImageMask", false))
            {
                // Stencil masks are already bitonal and they are painted using the
                // current fill color, so converting them makes no sense.
                return;
            }

            if (foundImages.insert(reference).second)
            {
                references.push_back(reference);
            }
        });
    }

    return references;
}

QImage PDFBitonalDocumentCreator::convertImageToBitonal(const QImage& image,
                                                        PDFImageConversion::ConversionMethod conversionMethod,
                                                        int threshold,
                                                        QImage* alphaMask,
                                                        const PDFOperationControl* operationControl)
{
    if (image.isNull())
    {
        return QImage();
    }

    PDFImageConversion imageConversion;
    imageConversion.setConversionMethod(conversionMethod);
    imageConversion.setThreshold(threshold);
    imageConversion.setAlphaMode(PDFImageConversion::AlphaMode::Composite);
    imageConversion.setOperationControl(operationControl);
    imageConversion.setImage(image);

    if (!imageConversion.convert())
    {
        return QImage();
    }

    if (alphaMask)
    {
        *alphaMask = imageConversion.getConvertedAlphaMask();
    }

    return imageConversion.getConvertedImage();
}

PDFObject PDFBitonalDocumentCreator::createBitonalImageObject(const QImage& image)
{
    if (image.isNull())
    {
        return PDFObject();
    }

    try
    {
        // The image is coded as a single JBIG2 generic region - it is lossless and
        // it is several times smaller than the Flate compression of a scanned page
        PDFImage::ImageEncodeOptions options;
        options.compression = PDFImage::ImageCompression::JBIG2;
        options.colorMode = PDFImage::ImageColorMode::Monochrome;

        PDFStream stream = PDFImage::createStreamFromImage(image, options, nullptr);

        PDFDictionary dictionary = *stream.getDictionary();
        QByteArray content = *stream.getContent();

        return PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), std::move(content)));
    }
    catch (const PDFException&)
    {
        return PDFObject();
    }
}

QImage PDFBitonalDocumentCreator::createFillImage(QSize size, bool isBlack)
{
    QImage image(size.expandedTo(QSize(1, 1)), QImage::Format_Mono);

    // The default color table of the format Format_Mono maps the sample value 0
    // to the black color and the sample value 1 to the white color.
    image.fill(isBlack ? 0 : 1);

    return image;
}

QImage PDFBitonalDocumentCreator::createFillPreviewImage(const QImage& image, bool isBlack)
{
    if (image.isNull())
    {
        return QImage();
    }

    std::optional<QImage> mask = PDFImageConversion::createAlphaMask(image);

    if (!mask)
    {
        // The mask cannot be created, so the preview cannot be created either
        return QImage();
    }

    QImage alphaMask = std::move(*mask);

    if (alphaMask.isNull() || !isBlack)
    {
        // The image is fully opaque, so the whole area is filled. The white fill looks
        // the same in both cases - a transparent area is displayed as a blank white
        // paper, which is exactly the color of the fill.
        return createFillImage(image.size(), isBlack);
    }

    // In the mask, a set sample means an opaque pixel, which the format Format_Mono
    // displays as white. The filled image is the other way round - the opaque part
    // becomes black and the transparent part stays a blank white paper.
    alphaMask.invertPixels();
    return alphaMask;
}

void PDFBitonalDocumentCreator::renderPages(const std::vector<PDFInteger>& pageIndices,
                                            const std::function<QSize(const PDFPage*)>& pageSizeGetter,
                                            const std::function<void(PDFInteger, QImage)>& pageImageProcessor,
                                            const PDFOperationControl* operationControl) const
{
    if (pageIndices.empty())
    {
        return;
    }

    Q_ASSERT(m_rasterizerPool);

    const PDFCatalog* catalog = m_document->getCatalog();

    // Rasterizer always renders the page as it is displayed, i.e. with the page
    // rotation applied. We want the image in the coordinate system of the page,
    // so a rotated page is rendered into a transposed image, which is then rotated
    // back. Rotating by a multiple of 90 degrees is a lossless operation.
    auto imageSizeGetter = [&pageSizeGetter](const PDFPage* page) -> QSize
    {
        const QSize size = pageSizeGetter(page);
        const PageRotation rotation = page->getPageRotation();

        if (rotation == PageRotation::Rotate90 || rotation == PageRotation::Rotate270)
        {
            return size.transposed();
        }

        return size;
    };

    auto processImage = [catalog, &pageImageProcessor](PDFRenderedPageImage& renderedPageImage)
    {
        QImage image;

        if (!renderedPageImage.hasSevereError())
        {
            image = compositePageImageOntoWhite(renderedPageImage.pageImage);
        }
        else
        {
            // The renderer has skipped a part of the content of the page - for example
            // an image, whose stream is damaged - and it has rendered the rest. Such an
            // image must not replace the page, so the page is reported as failed by
            // a null image.
            image = QImage();
        }

        const PDFPage* page = catalog->getPage(size_t(renderedPageImage.pageIndex));

        if (!image.isNull() && page)
        {
            QTransform transform;

            switch (page->getPageRotation())
            {
                case PageRotation::Rotate90:
                    transform.rotate(-90);
                    break;

                case PageRotation::Rotate180:
                    transform.rotate(180);
                    break;

                case PageRotation::Rotate270:
                    transform.rotate(90);
                    break;

                default:
                    break;
            }

            if (!transform.isIdentity())
            {
                image = image.transformed(transform);
            }
        }

        pageImageProcessor(renderedPageImage.pageIndex, std::move(image));
    };

    m_rasterizerPool->render(pageIndices, imageSizeGetter, processImage, nullptr, operationControl);
}

QImage PDFBitonalDocumentCreator::renderPage(PDFInteger pageIndex, QSize size, const PDFOperationControl* operationControl) const
{
    QImage result;

    auto pageSizeGetter = [size](const PDFPage*) { return size; };
    auto pageImageProcessor = [&result](PDFInteger, QImage image) { result = std::move(image); };

    renderPages({ pageIndex }, pageSizeGetter, pageImageProcessor, operationControl);

    return result;
}

QSize PDFBitonalDocumentCreator::getPageImageSize(const PDFPage* page, int dpiResolution)
{
    Q_ASSERT(page);

    // The resolution comes from the caller, so it is clamped here - a page rasterized
    // at an extreme resolution would need gigabytes of memory for a single image.
    const int resolution = qBound(MINIMUM_DPI_RESOLUTION, dpiResolution, MAXIMUM_DPI_RESOLUTION);

    const QSizeF size = page->getMediaBox().size() * PDF_POINT_TO_INCH * resolution;
    return size.toSize().expandedTo(QSize(1, 1));
}

int PDFBitonalDocumentCreator::getEstimatedDpiResolution() const
{
    PDFDocumentDataLoaderDecorator loader(m_document);
    int dpiResolution = 0;

    // Scanned documents store the page as an image covering the whole page. We estimate
    // the resolution from the size of these images, so the rasterized page keeps the
    // details of the original scan. We do not know, which part of the page the image
    // actually covers (that would require an analysis of the content stream), so the
    // estimate is only used to raise the resolution above the default one - a small
    // logo must never lower it.
    const PDFCatalog* catalog = m_document->getCatalog();
    for (size_t pageIndex = 0, pageCount = catalog->getPageCount(); pageIndex < pageCount; ++pageIndex)
    {
        const PDFPage* page = catalog->getPage(pageIndex);

        if (!page)
        {
            continue;
        }

        const QRectF mediaBox = page->getMediaBox();
        if (mediaBox.width() < 1.0 || mediaBox.height() < 1.0)
        {
            continue;
        }

        traversePageImages(page, [&](PDFObjectReference, const PDFDictionary* imageDictionary)
        {
            const PDFInteger width = loader.readIntegerFromDictionary(imageDictionary, "Width", 0);
            const PDFInteger height = loader.readIntegerFromDictionary(imageDictionary, "Height", 0);

            // An image inside a form XObject is scaled by the matrix of the form, which
            // is not analyzed here, so the estimate is even rougher for it. It can only
            // raise the resolution, so an inaccurate estimate cannot lose any detail.
            if (width > 0)
            {
                dpiResolution = qMax(dpiResolution, qRound(width / (mediaBox.width() * PDF_POINT_TO_INCH)));
            }

            if (height > 0)
            {
                dpiResolution = qMax(dpiResolution, qRound(height / (mediaBox.height() * PDF_POINT_TO_INCH)));
            }
        });
    }

    return qBound(DEFAULT_DPI_RESOLUTION, dpiResolution, MAXIMUM_DPI_RESOLUTION);
}

bool PDFBitonalDocumentCreator::isStencilMask(PDFObjectReference reference) const
{
    if (const PDFDictionary* dictionary = m_document->getDictionaryFromObject(m_document->getObjectByReference(reference)))
    {
        PDFDocumentDataLoaderDecorator loader(m_document);
        return loader.readBooleanFromDictionary(dictionary, "ImageMask", false);
    }

    return false;
}

std::optional<PDFImage> PDFBitonalDocumentCreator::getImageFromReference(PDFObjectReference reference) const
{
    std::optional<PDFImage> pdfImage;
    PDFObject imageObject = m_document->getObjectByReference(reference);
    PDFRenderErrorReporterDummy errorReporter;

    if (!imageObject.isStream())
    {
        // Image is not stream
        return pdfImage;
    }

    const PDFStream* stream = imageObject.getStream();
    try
    {
        PDFColorSpacePointer colorSpace;
        const PDFDictionary* streamDictionary = stream->getDictionary();
        if (streamDictionary->hasKey("ColorSpace"))
        {
            const PDFObject& colorSpaceObject = m_document->getObject(streamDictionary->get("ColorSpace"));
            if (colorSpaceObject.isName() || colorSpaceObject.isArray())
            {
                PDFDictionary dummyDictionary;
                colorSpace = PDFAbstractColorSpace::createColorSpace(&dummyDictionary, m_document, colorSpaceObject);
            }
        }
        pdfImage.emplace(PDFImage::createImage(m_document,
                                               stream,
                                               colorSpace,
                                               false,
                                               RenderingIntent::Perceptual,
                                               &errorReporter));
    }
    catch (const PDFException&)
    {
        // Do nothing
    }

    return pdfImage;
}

QImage PDFBitonalDocumentCreator::getDecodedImage(PDFObjectReference reference, const PDFOperationControl* operationControl) const
{
    std::optional<PDFImage> pdfImage = getImageFromReference(reference);

    if (!pdfImage)
    {
        return QImage();
    }

    PDFCMSGeneric genericCms;
    PDFRenderErrorReporterDummy errorReporter;

    try
    {
        return pdfImage->getImage(&genericCms, &errorReporter, operationControl);
    }
    catch (const PDFException&)
    {
        return QImage();
    }
}

void PDFBitonalDocumentCreator::startProgress(size_t stepCount, QString text)
{
    if (!m_progress)
    {
        return;
    }

    ProgressStartupInfo info;
    info.showDialog = true;
    info.text = std::move(text);
    m_progress->start(stepCount, std::move(info));
}

void PDFBitonalDocumentCreator::stepProgress()
{
    if (m_progress)
    {
        m_progress->step();
    }
}

void PDFBitonalDocumentCreator::finishProgress()
{
    if (m_progress)
    {
        m_progress->finish();
    }
}

}   // namespace pdf
