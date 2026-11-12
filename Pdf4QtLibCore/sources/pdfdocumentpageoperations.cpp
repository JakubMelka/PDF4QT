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

#include "pdfdocumentpageoperations.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfobjectutils.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfoperationcontrol.h"

#include <algorithm>
#include <map>
#include <set>

namespace pdf
{

/// Helpers of the operations on the pages
class PDFDocumentPageOperationsHelper
{
public:
    PDFDocumentPageOperationsHelper() = delete;

    /// The rotation of a page turned by \p turn
    static PageRotation turnRotation(PageRotation rotation, PageRotation turn)
    {
        return getPageRotationCombined(turn, rotation);
    }

    /// Valid pages of the document, sorted, without duplicates
    static std::vector<PDFInteger> getValidPages(const PDFDocument* document, const std::vector<PDFInteger>& pages)
    {
        std::vector<PDFInteger> result;
        const PDFInteger pageCount = document ? PDFInteger(document->getCatalog()->getPageCount()) : 0;
        for (const PDFInteger page : pages)
        {
            if (page >= 0 && page < pageCount)
            {
                result.push_back(page);
            }
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    /// The index of each page object of the document (the ranges of the document parts, 14.12);
    /// false, when the page tree lists a page object more than once (its index is not defined)
    static bool getPageIndices(const PDFDocument* document, std::map<PDFObjectReference, PDFInteger>* indices)
    {
        bool isUnique = true;
        const PDFCatalog* catalog = document->getCatalog();
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            isUnique = indices->emplace(catalog->getPage(i)->getPageReference(), PDFInteger(i)).second && isUnique;
        }
        return isUnique;
    }

    /// The result keeps the version and the hash of the source data of the input
    static PDFDocument createDocument(PDFDocumentBuilder& builder, const PDFDocument* input)
    {
        PDFDocument built = builder.build();
        return PDFDocument(PDFObjectStorage(built.getStorage()), input->getInfo()->version, input->getSourceDataHash());
    }

    /// The own OCR layers of the pages stay bound to them (a change of PDF4QT, which does not touch the text)
    static PDFDocument rebindOcrLayers(const PDFDocument* input, PDFDocument output, const std::vector<std::pair<PDFInteger, PDFInteger>>& pages)
    {
        PDFDocumentBuilder builder(&output);
        if (PDFOCRTextLayerWriter::rebindFingerprintsOfPages(&builder, input, &output, pages).empty())
        {
            return output;
        }
        return createDocument(builder, input);
    }
};

/// Removal of pages (PDFDocumentPageOperations::removePages). The objects of the
/// document are read and written raw - the parsed structures do not say, where an
/// object is referenced. Every walk over the graph is one PDFObjectWalk (one set of the
/// visited objects for the whole walk, a depth, a budget): a hostile document cannot make
/// it loop or repeat a shared part.
///
/// With a page index (PDFDocumentPageOperations::keepPages, a part of Split) the removal
/// starts from the flattened page tree of the index, knows the removed pages and their
/// annotations by the index (a page or an annotation of the document, which is not kept)
/// and leaves the unreachable objects to PDFNewDocumentFinisher: no walk over the whole
/// document for each part (R2).
class PDFDocumentPageRemoval
{
public:
    explicit PDFDocumentPageRemoval(const PDFDocument* document, const std::vector<PDFInteger>& removedPages, const PDFOperationControl* control);
    explicit PDFDocumentPageRemoval(const PDFDocument* document, const PDFDocumentPageOperations::PageIndex* index, const std::vector<PDFInteger>& keptPages, const PDFOperationControl* control);

    /// Runs the removal; throws PDFException
    PDFDocument run(PDFDocumentPageOperations::RemovalStatistics* statistics);

private:
    using References = std::set<PDFObjectReference>;

    /// A page object of the document, which goes
    bool isRemovedPage(const PDFObjectReference& reference) const
    {
        return m_index ? (m_index->m_pages.count(reference) && !m_keptPageSet.count(reference)) : m_removedPageSet.count(reference) > 0;
    }

    /// An annotation of a page, which goes, not listed by a page, which stays
    bool isRemovedAnnotation(const PDFObjectReference& reference) const
    {
        return m_index ? (m_index->m_annotations.count(reference) && !m_keptAnnotations.count(reference)) : m_removedAnnotations.count(reference) > 0;
    }

    /// An object, which goes with the pages: a page, its annotations, a removed field, signature or layer
    bool isRemovedObject(const PDFObjectReference& reference) const
    {
        return m_removedObjects.count(reference) || (m_index && (isRemovedPage(reference) || isRemovedAnnotation(reference)));
    }

    /// Depth of the outline and of the name trees (an outline deeper than this is not changed below it)
    static constexpr int MAXIMUM_DEPTH = 256;

    /// Depth of the direct objects of one object, which are rewritten
    static constexpr int MAXIMUM_OBJECT_DEPTH = 64;

    bool isCanceled() const { return PDFOperationControl::isOperationCancelled(m_control); }

    const PDFObject& getObject(const PDFObject& object) const { return m_builder.getObject(object); }
    const PDFDictionary* getDictionary(const PDFObject& object) const { return m_builder.getDictionaryFromObject(object); }

    PDFDictionaryBuilder getCatalog() const;
    void setCatalog(PDFDictionaryBuilder catalog);

    /// Writes the value of an entry of a dictionary of the catalog: into the object, when the
    /// entry is a reference, else into the catalog (\p value null = the entry is removed)
    void setCatalogEntry(const char* key, PDFObject value);

    /// A dictionary of the catalog with an own object (Names, AcroForm, OCProperties) is
    /// written into its object, a direct one into the catalog
    void setCatalogDictionary(const char* key, PDFDictionaryBuilder dictionary);

    void collectPages();
    void collectNamedDestinations();
    bool isDestinationRemoved(const PDFObject& destination, int depth = 0) const;
    bool isActionRemoved(const PDFObject& action) const;

    /// Reads the entries of a name tree in the order of the tree
    void readNameTree(const PDFObject& node, int depth, PDFObjectWalk* walk, std::vector<std::pair<QByteArray, PDFObject>>& entries) const;

    /// A flat name tree of the entries (an empty one = null)
    static PDFObject createNameTree(std::vector<std::pair<QByteArray, PDFObject>> entries);

    void filterNameTrees();
    void filterOutline();

    /// What filterOutlineChildren has done with the children of an item
    struct OutlineChildren
    {
        PDFInteger visibleCount = 0;    ///< Visible descendants after the removal (PDF 2.0, 12.3.3)
        size_t originalCount = 0;       ///< Children of the item, which the walk has read
        size_t keptCount = 0;           ///< Children, which stay
        bool isChanged = false;         ///< An item of the subtree is removed or has lost its target: the counts above it change
    };

    /// Filters the children of an outline item, \p parent gets its new /First, /Last and /Count
    OutlineChildren filterOutlineChildren(PDFDictionaryBuilder& parent, int depth);

    void filterForm();
    bool filterField(const PDFObject& field, int depth, bool isSignatureParent, PDFObjectWalk* walk);

    /// Counts a removed terminal field; a signature field takes its signature dictionary with it
    void removeTerminalField(const PDFDictionary* dictionary, bool isSignature);
    void filterPagesOfDocument();
    PDFObject filterActions(const PDFObject& actions) const;
    void rewritePageLabels();
    void filterLayers();

    /// What filterDocumentPart has done with a node of the document parts
    enum class PartState
    {
        Kept,       ///< The node keeps a page (its range or its children may be smaller)
        Removed,    ///< No page of the node stays: it is a removed object
        Damaged     ///< The hierarchy is not a tree of ranges of pages (14.12.2, Table 409)
    };

    /// The document parts (14.12) of the pages, which stay: see removePages
    void filterDocumentParts();

    /// Filters a node of the document parts and its descendants; \p levelCount gets the levels
    /// of the kept nodes (the root is the level 1), \p originalLevelCount of all nodes
    PartState filterDocumentPart(const PDFObject& node, int depth, PDFObjectWalk* walk, const std::map<PDFObjectReference, PDFInteger>& pageIndices,
                                 int* levelCount, int* originalLevelCount);

    /// Removes /DPartRoot and /DPart of the pages, which stay
    void removeDocumentParts();

    /// Collects the objects reachable from the trailer; the catalog entry \p skippedCatalogKey
    /// is not followed, the removed objects are not entered when \p excludeRemoved. Throws
    /// PDFException, when the walk cannot read every object (the sweep would remove reachable
    /// objects).
    References collectReachable(const PDFObjectStorage* storage, bool excludeRemoved, const char* skippedCatalogKey) const;

    /// Removes every reference of the removed objects from the reachable objects and makes the
    /// objects, which were reachable before and are not reachable now, null
    void sweep(const References& reachableBefore);
    PDFObject stripReferences(const PDFObject& object, int depth, bool* isChanged) const;

    const PDFDocument* m_document;
    const PDFDocumentPageOperations::PageIndex* m_index = nullptr;  ///< Shared index of a part (keepPages), nullptr = removePages
    const PDFOperationControl* m_control;
    PDFDocumentBuilder m_builder;
    PDFDocumentPageOperations::RemovalStatistics m_statistics;

    std::vector<PDFInteger> m_removedIndices;
    std::vector<PDFInteger> m_keptIndices;
    std::vector<PDFObjectReference> m_keptPages;
    References m_keptPageSet;
    References m_removedPageSet;
    References m_keptAnnotations;
    References m_removedAnnotations;
    References m_removedObjects;        ///< The removed pages, their annotations, the removed fields, signatures and layers
    PDFObjectWalk m_outlineWalk;        ///< Each item of the outline is read once (a hostile outline, whose items come back, is not walked again for each path)
    bool m_isSignatureKept = false;     ///< A widget of a signature field stays (or a field, which was not looked at)
    std::set<QByteArray> m_removedDestinationNames;
};

PDFDocumentPageRemoval::PDFDocumentPageRemoval(const PDFDocument* document, const std::vector<PDFInteger>& removedPages, const PDFOperationControl* control) :
    m_document(document),
    m_control(control),
    m_builder(document),
    m_removedIndices(PDFDocumentPageOperationsHelper::getValidPages(document, removedPages)),
    m_outlineWalk(MAXIMUM_DEPTH)
{

}

PDFDocumentPageRemoval::PDFDocumentPageRemoval(const PDFDocument* document, const PDFDocumentPageOperations::PageIndex* index, const std::vector<PDFInteger>& keptPages, const PDFOperationControl* control) :
    m_document(document),
    m_index(index),
    m_control(control),
    m_builder(&index->m_flattenedDocument),
    m_keptIndices(PDFDocumentPageOperationsHelper::getValidPages(document, keptPages)),
    m_outlineWalk(MAXIMUM_DEPTH)
{

}

PDFDictionaryBuilder PDFDocumentPageRemoval::getCatalog() const
{
    return PDFObjectUtils::copyDictionary(m_builder.getStorage(), m_builder.getObjectByReference(m_builder.getCatalogReference()));
}

void PDFDocumentPageRemoval::setCatalog(PDFDictionaryBuilder catalog)
{
    m_builder.setObject(m_builder.getCatalogReference(), PDFObject::createDictionary(std::move(catalog)));
}

void PDFDocumentPageRemoval::setCatalogEntry(const char* key, PDFObject value)
{
    PDFDictionaryBuilder catalog = getCatalog();
    if (value.isNull())
    {
        catalog.removeEntry(key);
    }
    else
    {
        catalog.setEntry(PDFInplaceOrMemoryString(key), std::move(value));
    }
    setCatalog(std::move(catalog));
}

void PDFDocumentPageRemoval::setCatalogDictionary(const char* key, PDFDictionaryBuilder dictionary)
{
    const PDFObject entry = getCatalog().get(key);
    if (entry.isReference())
    {
        m_builder.setObject(entry.getReference(), PDFObject::createDictionary(std::move(dictionary)));
    }
    else
    {
        setCatalogEntry(key, PDFObject::createDictionary(std::move(dictionary)));
    }
}

void PDFDocumentPageRemoval::collectPages()
{
    const PDFCatalog* catalog = m_document->getCatalog();
    if (m_index)
    {
        // A part: only its own pages are read, the other pages are known by the index
        if (m_keptIndices.empty())
        {
            throw PDFException(PDFDocumentPageOperations::tr("A document must keep at least one page."));
        }
        for (const PDFInteger index : m_keptIndices)
        {
            const PDFPage* page = catalog->getPage(size_t(index));
            m_keptPages.push_back(page->getPageReference());
            m_keptPageSet.insert(page->getPageReference());
            for (const PDFObjectReference& annotation : page->getAnnotations())
            {
                m_keptAnnotations.insert(annotation);
            }
        }
        return;
    }

    const size_t pageCount = catalog->getPageCount();
    const std::set<PDFInteger> removed(m_removedIndices.cbegin(), m_removedIndices.cend());

    for (size_t i = 0; i < pageCount; ++i)
    {
        const PDFPage* page = catalog->getPage(i);
        if (removed.count(PDFInteger(i)))
        {
            continue;
        }
        m_keptIndices.push_back(PDFInteger(i));
        m_keptPages.push_back(page->getPageReference());
        m_keptPageSet.insert(page->getPageReference());
        for (const PDFObjectReference& annotation : page->getAnnotations())
        {
            m_keptAnnotations.insert(annotation);
        }
    }

    if (m_keptIndices.empty())
    {
        throw PDFException(PDFDocumentPageOperations::tr("A document must keep at least one page."));
    }

    // A page, which the page tree lists also among the pages, which stay, is no removed page
    for (const PDFInteger index : m_removedIndices)
    {
        const PDFPage* page = catalog->getPage(size_t(index));
        if (!m_keptPageSet.count(page->getPageReference()))
        {
            m_removedPageSet.insert(page->getPageReference());
        }
        for (const PDFObjectReference& annotation : page->getAnnotations())
        {
            if (!m_keptAnnotations.count(annotation))
            {
                m_removedAnnotations.insert(annotation);
            }
        }
    }
    m_removedPageSet.erase(PDFObjectReference());
    m_removedAnnotations.erase(PDFObjectReference());

    m_removedObjects = m_removedPageSet;
    m_removedObjects.insert(m_removedAnnotations.cbegin(), m_removedAnnotations.cend());
}

void PDFDocumentPageRemoval::readNameTree(const PDFObject& node, int depth, PDFObjectWalk* walk, std::vector<std::pair<QByteArray, PDFObject>>& entries) const
{
    if (!walk->enter(node, depth))
    {
        return;
    }

    const PDFDictionary* dictionary = getDictionary(node);
    if (!dictionary)
    {
        return;
    }

    const PDFObject& names = getObject(dictionary->get("Names"));
    if (names.isArray())
    {
        const PDFArray* array = names.getArray();
        for (size_t i = 0; i + 1 < array->getCount(); i += 2)
        {
            const PDFObject& key = getObject(array->getItem(i));
            if (key.isString() || key.isName())
            {
                entries.emplace_back(key.getString(), array->getItem(i + 1));
            }
        }
    }

    const PDFObject& kids = getObject(dictionary->get("Kids"));
    if (kids.isArray())
    {
        for (const PDFObject& kid : *kids.getArray())
        {
            readNameTree(kid, depth + 1, walk, entries);
        }
    }
}

PDFObject PDFDocumentPageRemoval::createNameTree(std::vector<std::pair<QByteArray, PDFObject>> entries)
{
    if (entries.empty())
    {
        return PDFObject();
    }

    // A flat tree: the root with its /Names in the order of the keys (PDF 2.0, 7.9.6)
    std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    PDFArrayBuilder names;
    names.reserve(entries.size() * 2);
    for (auto& [key, value] : entries)
    {
        names.appendItem(PDFObject::createString(key));
        names.appendItem(std::move(value));
    }

    PDFDictionaryBuilder root;
    root.setEntry(PDFInplaceOrMemoryString("Names"), PDFObject::createArray(std::move(names)));
    return PDFObject::createDictionary(std::move(root));
}

void PDFDocumentPageRemoval::collectNamedDestinations()
{
    // Before any rewriting: the outline and the links name the destinations
    const PDFDictionaryBuilder catalog = getCatalog();
    if (const PDFDictionary* names = getDictionary(catalog.get("Names")))
    {
        std::vector<std::pair<QByteArray, PDFObject>> entries;
        PDFObjectWalk walk(MAXIMUM_DEPTH);
        readNameTree(names->get("Dests"), 0, &walk, entries);
        for (const auto& [name, value] : entries)
        {
            if (isDestinationRemoved(value))
            {
                m_removedDestinationNames.insert(name);
            }
        }
    }

    if (const PDFDictionary* dests = getDictionary(catalog.get("Dests")))
    {
        for (size_t i = 0; i < dests->getCount(); ++i)
        {
            if (isDestinationRemoved(dests->getValue(i)))
            {
                m_removedDestinationNames.insert(dests->getKey(i).getString());
            }
        }
    }
}

bool PDFDocumentPageRemoval::isDestinationRemoved(const PDFObject& destination, int depth) const
{
    const PDFObject& object = getObject(destination);
    if (object.isName() || object.isString())
    {
        // A named destination of a name tree is a dictionary with /D (PDF 2.0, 12.3.2.4) - its name is checked by the caller
        return depth == 0 && m_removedDestinationNames.count(object.getString());
    }
    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        return array->getCount() > 0 && array->getItem(0).isReference() && isRemovedPage(array->getItem(0).getReference());
    }
    if (const PDFDictionary* dictionary = getDictionary(object); dictionary && depth == 0)
    {
        return isDestinationRemoved(dictionary->get("D"), depth + 1);
    }
    return false;
}

bool PDFDocumentPageRemoval::isActionRemoved(const PDFObject& action) const
{
    const PDFDictionary* dictionary = getDictionary(action);
    if (!dictionary)
    {
        return false;
    }

    const PDFObject& type = getObject(dictionary->get("S"));
    return type.isName() && type.getString() == "GoTo" && isDestinationRemoved(dictionary->get("D"));
}

void PDFDocumentPageRemoval::filterNameTrees()
{
    PDFDictionaryBuilder catalog = getCatalog();

    // The legacy dictionary of the destinations (PDF 1.1)
    if (const PDFDictionary* dests = getDictionary(catalog.get("Dests")))
    {
        PDFDictionaryBuilder filtered;
        for (size_t i = 0; i < dests->getCount(); ++i)
        {
            if (m_removedDestinationNames.count(dests->getKey(i).getString()))
            {
                ++m_statistics.namedDestinationsRemoved;
                continue;
            }
            filtered.addEntry(dests->getKey(i), dests->getValue(i));
        }
        if (filtered.getCount() != dests->getCount())
        {
            setCatalogDictionary("Dests", std::move(filtered));
        }
    }

    catalog = getCatalog();
    const PDFDictionary* names = getDictionary(catalog.get("Names"));
    if (!names)
    {
        return;
    }

    PDFDictionaryBuilder newNames(*names);
    bool isChanged = false;
    for (const char* key : { "Dests", "Pages", "Templates" })
    {
        if (!names->hasKey(key))
        {
            continue;
        }

        std::vector<std::pair<QByteArray, PDFObject>> entries;
        PDFObjectWalk walk(MAXIMUM_DEPTH);
        readNameTree(names->get(key), 0, &walk, entries);

        std::vector<std::pair<QByteArray, PDFObject>> kept;
        const bool isDestinations = qstrcmp(key, "Dests") == 0;
        for (auto& entry : entries)
        {
            const bool isRemoved = isDestinations ? m_removedDestinationNames.count(entry.first) > 0
                                                  : entry.second.isReference() && isRemovedPage(entry.second.getReference());
            if (isRemoved)
            {
                ++(isDestinations ? m_statistics.namedDestinationsRemoved : m_statistics.namedPagesRemoved);
                continue;
            }
            kept.push_back(std::move(entry));
        }

        if (kept.size() != entries.size())
        {
            isChanged = true;
            PDFObject tree = createNameTree(std::move(kept));
            if (tree.isNull())
            {
                newNames.removeEntry(key);
            }
            else
            {
                newNames.setEntry(PDFInplaceOrMemoryString(key), std::move(tree));
            }
        }
    }

    if (isChanged)
    {
        setCatalogDictionary("Names", std::move(newNames));
    }
}

PDFDocumentPageRemoval::OutlineChildren PDFDocumentPageRemoval::filterOutlineChildren(PDFDictionaryBuilder& parent, int depth)
{
    struct KeptItem
    {
        PDFObjectReference reference;
        PDFDictionaryBuilder dictionary;
        bool isChanged = false;
    };

    OutlineChildren children;

    // A deeper outline is not changed below the limit
    if (depth > MAXIMUM_DEPTH)
    {
        children.originalCount = parent.get("First").isReference() ? 1 : 0;
        children.keptCount = children.originalCount;
        return children;
    }

    std::vector<KeptItem> keptItems;

    // The chain of the siblings: /First, then /Next; a hostile chain ends at an object, which
    // is no item (it has no /Title), which was seen already anywhere in the outline (an item
    // is walked once, also when more items lead to it) or which is a page
    PDFObject next = parent.get("First");
    while (next.isReference())
    {
        const PDFObjectReference reference = next.getReference();
        const PDFDictionary* dictionary = getDictionary(next);
        if (!dictionary || !dictionary->hasKey("Title") || m_keptPageSet.count(reference) || isRemovedPage(reference) || !m_outlineWalk.enter(next, depth))
        {
            break;
        }
        ++children.originalCount;
        next = dictionary->get("Next");

        KeptItem item;
        item.reference = reference;
        item.dictionary = PDFDictionaryBuilder(*dictionary);

        const OutlineChildren itemChildren = dictionary->get("First").isReference() ? filterOutlineChildren(item.dictionary, depth + 1) : OutlineChildren();
        item.isChanged = !(item.dictionary == PDFDictionaryBuilder(*dictionary));

        const bool isTargetRemoved = isDestinationRemoved(dictionary->get("Dest")) || isActionRemoved(dictionary->get("A"));
        const bool hasOtherTarget = !isTargetRemoved && (dictionary->hasKey("Dest") || dictionary->hasKey("A"));
        const bool isRemoved = (isTargetRemoved && itemChildren.keptCount == 0) || (itemChildren.originalCount > 0 && itemChildren.keptCount == 0 && !hasOtherTarget);
        if (isRemoved)
        {
            ++m_statistics.outlineItemsRemoved;
            children.isChanged = true;
            continue;
        }

        if (isTargetRemoved)
        {
            // A heading of the items below it, which stay
            item.dictionary.removeEntry("Dest");
            item.dictionary.removeEntry("A");
            item.isChanged = true;
            ++m_statistics.outlineTargetsRemoved;
        }

        const PDFDocumentDataLoaderDecorator loader(m_builder.getStorage());
        const bool isOpen = loader.readIntegerFromDictionary(dictionary, "Count", 0) > 0;
        children.visibleCount += 1 + (isOpen ? itemChildren.visibleCount : 0);
        children.isChanged = children.isChanged || (isOpen && itemChildren.isChanged);
        keptItems.push_back(std::move(item));
    }

    // The links of the siblings: only an object, whose links or content change, is written
    for (size_t i = 0; i < keptItems.size(); ++i)
    {
        KeptItem& item = keptItems[i];
        const PDFObject previous = i > 0 ? PDFObject::createReference(keptItems[i - 1].reference) : PDFObject();
        const PDFObject following = i + 1 < keptItems.size() ? PDFObject::createReference(keptItems[i + 1].reference) : PDFObject();
        const auto updateLink = [&item](const char* key, const PDFObject& value)
        {
            if (!(item.dictionary.get(key) == value))
            {
                if (value.isNull())
                {
                    item.dictionary.removeEntry(key);
                }
                else
                {
                    item.dictionary.setEntry(PDFInplaceOrMemoryString(key), value);
                }
                item.isChanged = true;
            }
        };
        updateLink("Prev", previous);
        updateLink("Next", following);

        if (item.isChanged)
        {
            m_builder.setObject(item.reference, PDFObject::createDictionary(std::move(item.dictionary)));
        }
    }

    const bool isChainChanged = keptItems.size() != children.originalCount;
    if (keptItems.empty())
    {
        parent.removeEntry("First");
        parent.removeEntry("Last");
        parent.removeEntry("Count");
    }
    else if (isChainChanged || !(parent.get("Last") == PDFObject::createReference(keptItems.back().reference)))
    {
        parent.setEntry(PDFInplaceOrMemoryString("First"), PDFObject::createReference(keptItems.front().reference));
        parent.setEntry(PDFInplaceOrMemoryString("Last"), PDFObject::createReference(keptItems.back().reference));
    }

    // /Count: positive of an open item, the negative number of the descendants, which would be
    // visible, of a closed one (PDF 2.0, Table 153); the root has the number of the visible items.
    // A removal deeper in the outline changes the counts of all open items above it.
    if (!keptItems.empty() && children.isChanged)
    {
        const PDFDocumentDataLoaderDecorator loader(m_builder.getStorage());
        const PDFInteger oldCount = loader.readInteger(parent.get("Count"), 0);
        const PDFInteger newCount = depth == 0 || oldCount > 0 ? children.visibleCount : -children.visibleCount;
        parent.setEntry(PDFInplaceOrMemoryString("Count"), PDFObject::createInteger(newCount));
    }

    children.keptCount = keptItems.size();
    return children;
}

void PDFDocumentPageRemoval::filterOutline()
{
    const PDFObject outlines = getCatalog().get("Outlines");
    const PDFDictionary* root = getDictionary(outlines);
    if (!root)
    {
        return;
    }

    PDFDictionaryBuilder newRoot(*root);
    const OutlineChildren children = filterOutlineChildren(newRoot, 0);
    if (newRoot == PDFDictionaryBuilder(*root))
    {
        return;
    }

    if (children.keptCount == 0)
    {
        setCatalogEntry("Outlines", PDFObject());
    }
    else
    {
        setCatalogDictionary("Outlines", std::move(newRoot));
    }
}

bool PDFDocumentPageRemoval::filterField(const PDFObject& field, int depth, bool isSignatureParent, PDFObjectWalk* walk)
{
    // \returns true = the field stays (a field, which is not walked, stays with its type)
    if (depth > PDFFormField::MAX_FIELD_TREE_DEPTH || !field.isReference())
    {
        const PDFDictionary* dictionary = getDictionary(field);
        const PDFObject& fieldType = dictionary ? getObject(dictionary->get("FT")) : PDFObject();
        m_isSignatureKept = m_isSignatureKept || (fieldType.isName() ? fieldType.getString() == "Sig" : isSignatureParent);
        return true;
    }

    const PDFObjectReference reference = field.getReference();
    if (!walk->enter(field, depth))
    {
        return true;
    }

    const PDFDictionary* dictionary = getDictionary(field);
    if (!dictionary)
    {
        return true;
    }

    const PDFObject& fieldType = getObject(dictionary->get("FT"));
    const bool isSignature = fieldType.isName() ? fieldType.getString() == "Sig" : isSignatureParent;
    const PDFObject& kids = getObject(dictionary->get("Kids"));
    if (kids.isArray() && kids.getArray()->getCount() > 0)
    {
        const qint64 fieldsRemovedBefore = m_statistics.fieldsRemoved;
        PDFArrayBuilder keptKids;
        for (const PDFObject& kid : *kids.getArray())
        {
            if (filterField(kid, depth + 1, isSignature, walk))
            {
                keptKids.appendItem(kid);
            }
        }

        if (keptKids.isEmpty())
        {
            // A terminal field, whose kids are its widgets (PDF 2.0, 12.7.4.1): no kid has counted
            // itself as a field, the field is removed with its last widget
            if (m_statistics.fieldsRemoved == fieldsRemovedBefore)
            {
                removeTerminalField(dictionary, isSignature);
            }

            // A field without kids goes everywhere (the calculation order /CO, also below a
            // parent, which stays), not only from the /Kids of its parent
            m_removedObjects.insert(reference);
            return false;
        }

        if (keptKids.getCount() != kids.getArray()->getCount())
        {
            const PDFObject kidsEntry = dictionary->get("Kids");
            if (kidsEntry.isReference())
            {
                m_builder.setObject(kidsEntry.getReference(), PDFObject::createArray(std::move(keptKids)));
            }
            else
            {
                PDFDictionaryBuilder newField(*dictionary);
                newField.setEntry(PDFInplaceOrMemoryString("Kids"), PDFObject::createArray(std::move(keptKids)));
                m_builder.setObject(reference, PDFObject::createDictionary(std::move(newField)));
            }
        }
        return true;
    }

    // A widget (a terminal field with its widget, or a kid): of a removed page, or a widget,
    // which says it is on a removed page and which no page, which stays, lists
    const PDFObject& page = dictionary->get("P");
    const bool isRemoved = isRemovedAnnotation(reference) ||
                           (page.isReference() && isRemovedPage(page.getReference()) && !m_keptAnnotations.count(reference));
    if (!isRemoved)
    {
        m_isSignatureKept = m_isSignatureKept || isSignature;
        return true;
    }

    m_removedObjects.insert(reference);
    const bool isTerminalField = dictionary->hasKey("T") || !dictionary->hasKey("Parent");
    if (isTerminalField)
    {
        removeTerminalField(dictionary, isSignature);
    }
    return false;
}

void PDFDocumentPageRemoval::removeTerminalField(const PDFDictionary* dictionary, bool isSignature)
{
    ++m_statistics.fieldsRemoved;
    if (isSignature)
    {
        // The signature dictionary goes with its field, also from /Perms (PDF 2.0, 12.8.4)
        ++m_statistics.signaturesRemoved;
        const PDFObject& value = dictionary->get("V");
        if (value.isReference())
        {
            m_removedObjects.insert(value.getReference());
        }
    }
}

void PDFDocumentPageRemoval::filterForm()
{
    const PDFDictionaryBuilder catalog = getCatalog();
    const PDFDictionary* form = getDictionary(catalog.get("AcroForm"));
    if (!form)
    {
        return;
    }

    const PDFObject& fields = getObject(form->get("Fields"));
    if (!fields.isArray())
    {
        return;
    }

    PDFObjectWalk walk(PDFFormField::MAX_FIELD_TREE_DEPTH);
    PDFArrayBuilder keptFields;
    std::vector<PDFObjectReference> removedFields;
    for (const PDFObject& field : *fields.getArray())
    {
        if (filterField(field, 0, false, &walk))
        {
            keptFields.appendItem(field);
        }
        else if (field.isReference())
        {
            removedFields.push_back(field.getReference());
        }
    }

    // A field, which had its widgets only on the removed pages, is a removed object (/CO, /Perms)
    for (const PDFObjectReference& reference : removedFields)
    {
        m_removedObjects.insert(reference);
    }

    PDFDictionaryBuilder newForm(*form);
    if (keptFields.getCount() != fields.getArray()->getCount())
    {
        const PDFObject fieldsEntry = form->get("Fields");
        if (fieldsEntry.isReference())
        {
            m_builder.setObject(fieldsEntry.getReference(), PDFObject::createArray(std::move(keptFields)));
        }
        else
        {
            newForm.setEntry(PDFInplaceOrMemoryString("Fields"), PDFObject::createArray(std::move(keptFields)));
        }
    }

    // No signature field stays: the flags of the signatures say nothing any more (PDF 2.0, Table 225)
    if (m_statistics.signaturesRemoved > 0 && !m_isSignatureKept)
    {
        newForm.removeEntry("SigFlags");
    }

    if (!(newForm == PDFDictionaryBuilder(*form)))
    {
        setCatalogDictionary("AcroForm", std::move(newForm));
    }
}

PDFObject PDFDocumentPageRemoval::filterActions(const PDFObject& actions) const
{
    // The additional actions of a page or of an annotation: a GoTo to a removed page is removed
    const PDFDictionary* dictionary = getDictionary(actions);
    if (!dictionary)
    {
        return actions;
    }

    PDFDictionaryBuilder filtered;
    for (size_t i = 0; i < dictionary->getCount(); ++i)
    {
        if (!isActionRemoved(dictionary->getValue(i)))
        {
            filtered.addEntry(dictionary->getKey(i), dictionary->getValue(i));
        }
    }
    if (filtered.getCount() == dictionary->getCount())
    {
        return actions;
    }
    return filtered.isEmpty() ? PDFObject() : PDFObject::createDictionary(std::move(filtered));
}

void PDFDocumentPageRemoval::filterPagesOfDocument()
{
    for (const PDFObjectReference& pageReference : m_keptPages)
    {
        const PDFDictionary* page = getDictionary(m_builder.getObjectByReference(pageReference));
        if (!page)
        {
            continue;
        }

        PDFDictionaryBuilder newPage(*page);

        // The beads of the articles (the articles are removed, PDF 2.0, 12.4.3)
        if (m_statistics.threadsRemoved)
        {
            newPage.removeEntry("B");
        }

        const PDFObject pageActions = filterActions(page->get("AA"));
        if (!(pageActions == page->get("AA")))
        {
            ++m_statistics.actionsRemoved;
            if (pageActions.isNull())
            {
                newPage.removeEntry("AA");
            }
            else
            {
                newPage.setEntry(PDFInplaceOrMemoryString("AA"), pageActions);
            }
        }

        const PDFObject annotsEntry = page->get("Annots");
        const PDFObject& annots = getObject(annotsEntry);
        if (annots.isArray())
        {
            PDFArrayBuilder keptAnnotations;
            for (const PDFObject& annotation : *annots.getArray())
            {
                const PDFDictionary* dictionary = getDictionary(annotation);
                if (!dictionary)
                {
                    keptAnnotations.appendItem(annotation);
                    continue;
                }

                if (annotation.isReference() && isRemovedObject(annotation.getReference()))
                {
                    continue;
                }

                const PDFObject& subtype = getObject(dictionary->get("Subtype"));
                const bool isTargetRemoved = isDestinationRemoved(dictionary->get("Dest")) || isActionRemoved(dictionary->get("A"));
                if (subtype.isName() && subtype.getString() == "Link" && isTargetRemoved)
                {
                    ++m_statistics.linksRemoved;
                    if (annotation.isReference())
                    {
                        m_removedObjects.insert(annotation.getReference());
                    }
                    continue;
                }

                // Another annotation keeps itself and loses only the action (a button going to a removed page)
                PDFDictionaryBuilder newAnnotation(*dictionary);
                if (isTargetRemoved)
                {
                    newAnnotation.removeEntry("Dest");
                    newAnnotation.removeEntry("A");
                    ++m_statistics.actionsRemoved;
                }
                const PDFObject annotationActions = filterActions(dictionary->get("AA"));
                if (!(annotationActions == dictionary->get("AA")))
                {
                    ++m_statistics.actionsRemoved;
                    if (annotationActions.isNull())
                    {
                        newAnnotation.removeEntry("AA");
                    }
                    else
                    {
                        newAnnotation.setEntry(PDFInplaceOrMemoryString("AA"), annotationActions);
                    }
                }

                if (newAnnotation == PDFDictionaryBuilder(*dictionary))
                {
                    keptAnnotations.appendItem(annotation);
                }
                else if (annotation.isReference())
                {
                    m_builder.setObject(annotation.getReference(), PDFObject::createDictionary(std::move(newAnnotation)));
                    keptAnnotations.appendItem(annotation);
                }
                else
                {
                    keptAnnotations.appendItem(PDFObject::createDictionary(std::move(newAnnotation)));
                }
            }

            const PDFObject newAnnots = PDFObject::createArray(std::move(keptAnnotations));
            if (!(newAnnots == annots))
            {
                if (annotsEntry.isReference())
                {
                    m_builder.setObject(annotsEntry.getReference(), newAnnots);
                }
                else
                {
                    newPage.setEntry(PDFInplaceOrMemoryString("Annots"), newAnnots);
                }
            }
        }

        if (!(newPage == PDFDictionaryBuilder(*page)))
        {
            m_builder.setObject(pageReference, PDFObject::createDictionary(std::move(newPage)));
        }
    }
}

void PDFDocumentPageRemoval::rewritePageLabels()
{
    // Each page, which stays, keeps its label: the number tree is written again for the new
    // indices of the pages (PDF 2.0, 12.4.2); a range of labels continues as long as its
    // numbers follow each other
    const PDFCatalog* catalog = m_document->getCatalog();
    const std::vector<PDFPageLabel>& labels = catalog->getPageLabels();
    const PDFDictionaryBuilder catalogDictionary = getCatalog();
    if (labels.empty() || !catalogDictionary.hasKey("PageLabels"))
    {
        return;
    }

    std::map<PDFInteger, PDFObject> rawLabels;
    for (const auto& [index, label] : PDFObjectUtils::readNumberTree(m_builder.getStorage(), catalogDictionary.get("PageLabels")))
    {
        rawLabels.emplace(index, label);
    }

    PDFArrayBuilder nums;
    const PDFPageLabel* previousLabel = nullptr;
    PDFInteger previousNumber = 0;
    for (size_t newIndex = 0; newIndex < m_keptIndices.size(); ++newIndex)
    {
        const PDFInteger oldIndex = m_keptIndices[newIndex];
        auto it = std::upper_bound(labels.cbegin(), labels.cend(), PDFPageLabel(PDFPageLabel::NumberingStyle::None, QString(), oldIndex, 0));
        const PDFPageLabel* label = it != labels.cbegin() ? &*std::prev(it) : nullptr;
        const PDFInteger number = label ? label->getPageStartNumber() + (oldIndex - label->getPageIndex()) : oldIndex + 1;
        const bool isContinued = newIndex > 0 && label == previousLabel && (label && label->getNumberingStyle() == PDFPageLabel::NumberingStyle::None ? true : number == previousNumber + 1);
        previousLabel = label;
        previousNumber = number;
        if (isContinued)
        {
            continue;
        }

        // The entry of the label of the page with the number of the page
        PDFDictionaryBuilder entry;
        if (label && rawLabels.count(label->getPageIndex()))
        {
            entry = PDFObjectUtils::copyDictionary(m_builder.getStorage(), rawLabels.at(label->getPageIndex()));
        }
        else
        {
            // A page before the first label: its label was its number
            entry.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName(QByteArray("D")));
        }

        if (!label || label->getNumberingStyle() != PDFPageLabel::NumberingStyle::None)
        {
            if (number == 1)
            {
                entry.removeEntry("St");
            }
            else
            {
                entry.setEntry(PDFInplaceOrMemoryString("St"), PDFObject::createInteger(number));
            }
        }

        nums.appendItem(PDFObject::createInteger(PDFInteger(newIndex)));
        nums.appendItem(PDFObject::createDictionary(std::move(entry)));
    }

    PDFDictionaryBuilder tree;
    tree.setEntry(PDFInplaceOrMemoryString("Nums"), PDFObject::createArray(std::move(nums)));
    setCatalogEntry("PageLabels", PDFObject::createDictionary(std::move(tree)));
    m_statistics.pageLabelsRewritten = true;
}

PDFDocumentPageRemoval::References PDFDocumentPageRemoval::collectReachable(const PDFObjectStorage* storage, bool excludeRemoved, const char* skippedCatalogKey) const
{
    // The closure of the references: a reference is entered once, the stack holds each object
    // at most once for each reference to it (the work is linear in the size of the objects)
    PDFObjectWalk walk;
    std::vector<PDFObjectReference> stack;

    const PDFObject catalogReference = storage->getTrailerDictionary().isDictionary() ? storage->getTrailerDictionary().getDictionary()->get("Root") : PDFObject();
    for (const PDFObjectReference& reference : PDFObjectUtils::getDirectReferences(storage->getTrailerDictionary()))
    {
        stack.push_back(reference);
    }

    while (!stack.empty())
    {
        const PDFObjectReference reference = stack.back();
        stack.pop_back();
        if ((excludeRemoved && isRemovedObject(reference)) || !walk.enterReference(reference, 0))
        {
            continue;
        }

        const PDFObject& object = storage->getObject(reference);
        if (skippedCatalogKey && catalogReference.isReference() && catalogReference.getReference() == reference && object.isDictionary())
        {
            const PDFDictionary* catalog = object.getDictionary();
            for (size_t i = 0; i < catalog->getCount(); ++i)
            {
                if (catalog->getKey(i).getString() == skippedCatalogKey)
                {
                    continue;
                }
                for (const PDFObjectReference& child : PDFObjectUtils::getDirectReferences(catalog->getValue(i)))
                {
                    stack.push_back(child);
                }
            }
            continue;
        }

        for (const PDFObjectReference& child : PDFObjectUtils::getDirectReferences(object))
        {
            stack.push_back(child);
        }
    }

    if (walk.isLimitReached())
    {
        throw PDFException(PDFDocumentPageOperations::tr("The document has too many objects to be changed."));
    }
    return walk.getVisitedReferences();
}

void PDFDocumentPageRemoval::filterLayers()
{
    // An optional content group, which only the removed pages used, is removed from the
    // layers of the document (the panel Layers would list a layer, which does nothing)
    const PDFDictionaryBuilder catalog = getCatalog();
    const PDFDictionary* properties = getDictionary(catalog.get("OCProperties"));
    if (!properties)
    {
        return;
    }

    const PDFObject& groups = getObject(properties->get("OCGs"));
    if (!groups.isArray())
    {
        return;
    }

    // What the removed pages used before the removal, what the rest of the document uses now
    // (a part of Split: the layers, which a page of the document reaches, by the index - a
    // layer, which the part reaches now, stays)
    References usedByRemoved;
    if (m_index)
    {
        usedByRemoved = m_index->m_layersOfPages;
    }
    else
    {
        std::vector<PDFObject> roots;
        for (const PDFObjectReference& reference : m_removedObjects)
        {
            roots.push_back(PDFObject::createReference(reference));
        }
        usedByRemoved = PDFObjectUtils::getReferences(roots, m_document->getStorage());
    }
    const References usedNow = collectReachable(m_builder.getStorage(), true, "OCProperties");

    for (const PDFObject& group : *groups.getArray())
    {
        if (group.isReference() && usedByRemoved.count(group.getReference()) && !usedNow.count(group.getReference()))
        {
            m_removedObjects.insert(group.getReference());
            ++m_statistics.layersRemoved;
        }
    }
}

PDFDocumentPageRemoval::PartState PDFDocumentPageRemoval::filterDocumentPart(const PDFObject& node, int depth, PDFObjectWalk* walk, const std::map<PDFObjectReference, PDFInteger>& pageIndices,
                                                                             int* levelCount, int* originalLevelCount)
{
    // 14.12.2: a node is an indirect DPart dictionary of one parent - a node reached again (a cycle,
    // a node of two parents) or deeper than the limit is no tree
    if (!node.isReference() || !walk->enter(node, depth))
    {
        return PartState::Damaged;
    }
    const PDFDictionary* dictionary = getDictionary(node);
    if (!dictionary)
    {
        return PartState::Damaged;
    }
    *originalLevelCount = qMax(*originalLevelCount, depth + 1);

    // Table 409: a node has either /DParts (an inner node) or /Start (a leaf)
    const bool hasParts = dictionary->hasKey("DParts");
    if (hasParts == dictionary->hasKey("Start"))
    {
        return PartState::Damaged;
    }

    if (!hasParts)
    {
        // A leaf: the range of pages from /Start to /End (/End only for more pages, both indirect
        // references to page objects, 14.12.3: the order of the page tree)
        const PDFObject& start = dictionary->get("Start");
        const PDFObject& end = dictionary->hasKey("End") ? dictionary->get("End") : start;
        if (!start.isReference() || !end.isReference())
        {
            return PartState::Damaged;
        }
        const auto startIt = pageIndices.find(start.getReference());
        const auto endIt = pageIndices.find(end.getReference());
        if (startIt == pageIndices.cend() || endIt == pageIndices.cend() || endIt->second < startIt->second)
        {
            return PartState::Damaged;
        }

        // The pages of the range, which stay: the order is kept, so they are a range again
        const auto first = std::lower_bound(m_keptIndices.cbegin(), m_keptIndices.cend(), startIt->second);
        const auto last = std::upper_bound(m_keptIndices.cbegin(), m_keptIndices.cend(), endIt->second);
        if (first == last)
        {
            m_removedObjects.insert(node.getReference());
            return PartState::Removed;
        }
        *levelCount = qMax(*levelCount, depth + 1);
        if (*first == startIt->second && *std::prev(last) == endIt->second)
        {
            return PartState::Kept;
        }

        const PDFCatalog* catalog = m_document->getCatalog();
        const PDFObjectReference newStart = catalog->getPage(size_t(*first))->getPageReference();
        const PDFObjectReference newEnd = catalog->getPage(size_t(*std::prev(last)))->getPageReference();
        PDFDictionaryBuilder leaf(*dictionary);
        leaf.setEntry(PDFInplaceOrMemoryString("Start"), PDFObject::createReference(newStart));
        if (newStart == newEnd)
        {
            // Table 409: /End only for a range of more pages
            leaf.removeEntry("End");
        }
        else
        {
            leaf.setEntry(PDFInplaceOrMemoryString("End"), PDFObject::createReference(newEnd));
        }
        m_builder.setObject(node.getReference(), PDFObject::createDictionary(std::move(leaf)));
        return PartState::Kept;
    }

    // An inner node: /DParts is an array of arrays of the children, no array is empty (Table 409)
    const PDFObject& parts = getObject(dictionary->get("DParts"));
    if (!parts.isArray() || parts.getArray()->getCount() == 0)
    {
        return PartState::Damaged;
    }
    PDFArrayBuilder newParts;
    bool isChanged = false;
    for (const PDFObject& partsItem : *parts.getArray())
    {
        const PDFObject& children = getObject(partsItem);
        if (!children.isArray() || children.getArray()->getCount() == 0)
        {
            return PartState::Damaged;
        }
        PDFArrayBuilder keptChildren;
        for (const PDFObject& child : *children.getArray())
        {
            switch (filterDocumentPart(child, depth + 1, walk, pageIndices, levelCount, originalLevelCount))
            {
                case PartState::Kept:
                    keptChildren.appendItem(child);
                    break;
                case PartState::Removed:
                    break;
                case PartState::Damaged:
                    return PartState::Damaged;
            }
        }
        if (keptChildren.getCount() == children.getArray()->getCount())
        {
            newParts.appendItem(partsItem);
        }
        else
        {
            isChanged = true;
            if (!keptChildren.isEmpty())
            {
                newParts.appendItem(PDFObject::createArray(std::move(keptChildren)));
            }
        }
    }

    if (newParts.isEmpty())
    {
        m_removedObjects.insert(node.getReference());
        return PartState::Removed;
    }
    *levelCount = qMax(*levelCount, depth + 1);
    if (isChanged)
    {
        PDFDictionaryBuilder newNode(*dictionary);
        newNode.setEntry(PDFInplaceOrMemoryString("DParts"), PDFObject::createArray(std::move(newParts)));
        m_builder.setObject(node.getReference(), PDFObject::createDictionary(std::move(newNode)));
    }
    return PartState::Kept;
}

void PDFDocumentPageRemoval::removeDocumentParts()
{
    m_statistics.documentPartsRemoved = true;
    setCatalogEntry("DPartRoot", PDFObject());
    for (const PDFObjectReference& pageReference : m_keptPages)
    {
        const PDFDictionary* page = getDictionary(m_builder.getObjectByReference(pageReference));
        if (page && page->hasKey("DPart"))
        {
            PDFDictionaryBuilder newPage(*page);
            newPage.removeEntry("DPart");
            m_builder.setObject(pageReference, PDFObject::createDictionary(std::move(newPage)));
        }
    }
}

void PDFDocumentPageRemoval::filterDocumentParts()
{
    // 14.12: the leaves of the document parts are ranges of pages in the order of the page tree,
    // which cover every page once. The pages, which stay, keep their order, so the pages of a
    // range, which stay, are a range again: /Start and /End move to the first and the last of
    // them, a leaf without a page and an inner node without a child go, /DPM and the other
    // entries of a node, which stays, are kept. /DPart of a page, which stays, names its leaf,
    // which stays (its range keeps the page). A hierarchy, which is no tree of ranges, is
    // removed whole with /DPart of the pages (nothing is guessed).
    const PDFDictionaryBuilder catalog = getCatalog();
    if (!catalog.hasKey("DPartRoot"))
    {
        return;
    }

    std::map<PDFObjectReference, PDFInteger> localPageIndices;
    const bool isPageTreeValid = m_index ? !m_index->m_hasRepeatedPage : PDFDocumentPageOperationsHelper::getPageIndices(m_document, &localPageIndices);
    const std::map<PDFObjectReference, PDFInteger>& pageIndices = m_index ? m_index->m_pageIndices : localPageIndices;
    const PDFDictionary* root = getDictionary(catalog.get("DPartRoot"));
    if (!isPageTreeValid || !root)
    {
        removeDocumentParts();
        return;
    }

    // Table 408: /DPartRootNode is an indirect reference to the root node; one walk over the
    // whole hierarchy with the depth limit of the other trees
    PDFObjectWalk walk(MAXIMUM_DEPTH);
    int levelCount = 0;
    int originalLevelCount = 0;
    const PartState state = filterDocumentPart(root->get("DPartRootNode"), 0, &walk, pageIndices, &levelCount, &originalLevelCount);
    if (state != PartState::Kept)
    {
        removeDocumentParts();
        return;
    }

    // Table 408: /NodeNameList has a name per level, /RecordLevel is a level (from 0) - a level,
    // which is gone, goes from them
    if (levelCount < originalLevelCount)
    {
        PDFDictionaryBuilder newRoot(*root);
        const PDFObject& names = getObject(root->get("NodeNameList"));
        if (names.isArray() && names.getArray()->getCount() > size_t(levelCount))
        {
            PDFArrayBuilder keptNames;
            for (size_t i = 0; i < size_t(levelCount); ++i)
            {
                keptNames.appendItem(names.getArray()->getItem(i));
            }
            newRoot.setEntry(PDFInplaceOrMemoryString("NodeNameList"), PDFObject::createArray(std::move(keptNames)));
        }
        const PDFObject& recordLevel = getObject(root->get("RecordLevel"));
        if (recordLevel.isInt() && recordLevel.getInteger() >= levelCount)
        {
            newRoot.removeEntry("RecordLevel");
        }
        if (!(newRoot == PDFDictionaryBuilder(*root)))
        {
            setCatalogDictionary("DPartRoot", std::move(newRoot));
        }
    }
}

PDFObject PDFDocumentPageRemoval::stripReferences(const PDFObject& object, int depth, bool* isChanged) const
{
    if (depth > MAXIMUM_OBJECT_DEPTH)
    {
        return object;
    }

    switch (object.getType())
    {
        case PDFObject::Type::Array:
        {
            const PDFArray* array = object.getArray();
            PDFArrayBuilder result;
            bool isArrayChanged = false;
            for (const PDFObject& item : *array)
            {
                if (item.isReference() && isRemovedObject(item.getReference()))
                {
                    // An item of a list (layers, fields) or the page of a destination - a
                    // destination without its page is no destination (PDFDestination::parse)
                    isArrayChanged = true;
                    continue;
                }
                bool isItemChanged = false;
                result.appendItem(stripReferences(item, depth + 1, &isItemChanged));
                isArrayChanged = isArrayChanged || isItemChanged;
            }
            if (!isArrayChanged)
            {
                return object;
            }
            *isChanged = true;
            return PDFObject::createArray(std::move(result));
        }

        case PDFObject::Type::Dictionary:
        case PDFObject::Type::Stream:
        {
            const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
            PDFDictionaryBuilder result;
            bool isDictionaryChanged = false;
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                const PDFObject& value = dictionary->getValue(i);
                if (value.isReference() && isRemovedObject(value.getReference()))
                {
                    // A null value is no entry (PDF 2.0, 7.3.7)
                    isDictionaryChanged = true;
                    continue;
                }
                bool isValueChanged = false;
                result.addEntry(dictionary->getKey(i), stripReferences(value, depth + 1, &isValueChanged));
                isDictionaryChanged = isDictionaryChanged || isValueChanged;
            }
            if (!isDictionaryChanged)
            {
                return object;
            }
            *isChanged = true;
            if (object.isStream())
            {
                return PDFObject::createStream(PDFStream(std::move(result), QByteArray(*object.getStream()->getContent())));
            }
            return PDFObject::createDictionary(std::move(result));
        }

        default:
            return object;
    }
}

void PDFDocumentPageRemoval::sweep(const References& reachableBefore)
{
    // Every object, which stays, loses its references of the removed objects
    const PDFObjectStorage* storage = m_builder.getStorage();
    const References reachable = collectReachable(storage, true, nullptr);
    for (const PDFObjectReference& reference : reachable)
    {
        bool isChanged = false;
        PDFObject stripped = stripReferences(storage->getObject(reference), 0, &isChanged);
        if (isChanged)
        {
            m_builder.setObject(reference, std::move(stripped));
        }
    }

    // A part of Split is finished as a new document (PDFNewDocumentFinisher removes what nothing
    // refers to and numbers the objects anew): no walk over the whole document for each part
    if (m_index)
    {
        return;
    }

    // The objects, which nothing refers to now, are null; an object, which was not reachable
    // before (a container of the file structure, an old revision), is not a matter of this removal
    const References reachableAfter = collectReachable(m_builder.getStorage(), false, nullptr);
    for (const PDFObjectReference& reference : reachableBefore)
    {
        if (!reachableAfter.count(reference) && !m_builder.getObjectByReference(reference).isNull())
        {
            m_builder.setObject(reference, PDFObject());
            ++m_statistics.objectsRemoved;
        }
    }
}

PDFDocument PDFDocumentPageRemoval::run(PDFDocumentPageOperations::RemovalStatistics* statistics)
{
    collectPages();
    collectNamedDestinations();
    const References reachableBefore = m_index ? References() : collectReachable(&m_document->getStorage(), false, nullptr);

    // A flat page tree of the pages, which stay (the inherited attributes are written into the
    // pages; the builder of a part starts from the page tree flattened by the index)
    if (!m_index)
    {
        m_builder.flattenPageTree();
    }
    m_builder.setPages(m_keptPages);
    if (isCanceled())
    {
        m_statistics.isCanceled = true;
        *statistics = m_statistics;
        return PDFDocument();
    }

    // As PDFDocumentManipulator of the old PageMaster (owner answer Q8)
    const PDFDictionaryBuilder catalog = getCatalog();
    m_statistics.structureTreeRemoved = catalog.hasKey("StructTreeRoot");
    m_statistics.threadsRemoved = catalog.hasKey("Threads");
    m_statistics.documentActionsRemoved = catalog.hasKey("OpenAction") || catalog.hasKey("AA");
    m_builder.removeThreads();
    m_builder.removeDocumentActions();
    m_builder.removeStructureTree();

    filterNameTrees();
    filterOutline();
    filterForm();
    filterPagesOfDocument();
    if (isCanceled())
    {
        m_statistics.isCanceled = true;
        *statistics = m_statistics;
        return PDFDocument();
    }

    rewritePageLabels();
    filterDocumentParts();
    filterLayers();

    // /Perms of a removed signature (PDF 2.0, 12.8.4)
    if (const PDFDictionary* permissions = getDictionary(getCatalog().get("Perms")))
    {
        PDFDictionaryBuilder kept;
        for (size_t i = 0; i < permissions->getCount(); ++i)
        {
            const PDFObject& value = permissions->getValue(i);
            if (!(value.isReference() && isRemovedObject(value.getReference())))
            {
                kept.addEntry(permissions->getKey(i), value);
            }
        }
        if (kept.getCount() != permissions->getCount())
        {
            if (kept.isEmpty())
            {
                setCatalogEntry("Perms", PDFObject());
            }
            else
            {
                setCatalogDictionary("Perms", std::move(kept));
            }
        }
    }

    sweep(reachableBefore);
    if (isCanceled())
    {
        m_statistics.isCanceled = true;
        *statistics = m_statistics;
        return PDFDocument();
    }

    PDFDocument result = PDFDocumentPageOperationsHelper::createDocument(m_builder, m_document);

    // A result without a signature is written whole: the dead containers of the file structure,
    // which hold the objects of the removed pages compressed, go with it (a signed result is
    // saved by an incremental update, which keeps the numbers of the objects); a part of Split
    // is written whole by its finishing
    if (!m_index && PDFForm::getSignatureCount(&result) == 0)
    {
        result = PDFDocumentTool::createWholeFileDocument(PDFObjectStorage(result.getStorage()), m_document);
    }
    else if (m_index)
    {
        // A part keeps only the objects of its own pages and trees: the other entries are empty
        // (one walk over the part, an empty table of the size of the document), so the finishing
        // never visits the objects of the other parts (R2)
        PDFObjectStorage storage = result.getStorage();
        const References reachable = collectReachable(&storage, false, nullptr);
        const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
        PDFObjectStorage::PDFObjects partObjects(objects.size());
        for (const PDFObjectReference& reference : reachable)
        {
            if (reference.objectNumber >= 0 && size_t(reference.objectNumber) < objects.size())
            {
                partObjects[size_t(reference.objectNumber)] = objects[size_t(reference.objectNumber)];
            }
        }
        storage.setObjects(std::move(partObjects));
        result = PDFDocument(std::move(storage), result.getInfo()->version, result.getSourceDataHash());
    }

    std::vector<std::pair<PDFInteger, PDFInteger>> pageMap;
    for (size_t i = 0; i < m_keptIndices.size(); ++i)
    {
        pageMap.emplace_back(m_keptIndices[i], PDFInteger(i));
    }
    result = PDFDocumentPageOperationsHelper::rebindOcrLayers(m_document, std::move(result), pageMap);

    if (statistics)
    {
        *statistics = m_statistics;
    }
    return result;
}

std::vector<PDFDocumentPageOperations::PageRotationChange> PDFDocumentPageOperations::createTurnChanges(const PDFDocument* document, const std::vector<PDFInteger>& pages, PageRotation turn)
{
    std::vector<PageRotationChange> changes;
    if (!document)
    {
        return changes;
    }

    const PDFCatalog* catalog = document->getCatalog();
    for (const PDFInteger page : PDFDocumentPageOperationsHelper::getValidPages(document, pages))
    {
        changes.push_back({ page, PDFDocumentPageOperationsHelper::turnRotation(catalog->getPage(size_t(page))->getPageRotation(), turn) });
    }
    return changes;
}

PDFDocument PDFDocumentPageOperations::rotatePages(const PDFDocument* document, const std::vector<PageRotationChange>& changes)
{
    if (!document)
    {
        throw PDFException(tr("There is no document."));
    }

    const PDFCatalog* catalog = document->getCatalog();
    PDFDocumentBuilder builder(document);
    std::vector<std::pair<PDFInteger, PDFInteger>> rotatedPages;
    for (const PageRotationChange& change : changes)
    {
        if (change.pageIndex < 0 || size_t(change.pageIndex) >= catalog->getPageCount())
        {
            throw PDFException(tr("The document has no page %1.").arg(change.pageIndex + 1));
        }
        builder.setPageRotation(catalog->getPage(size_t(change.pageIndex))->getPageReference(), change.rotation);
        rotatedPages.emplace_back(change.pageIndex, change.pageIndex);
    }

    return PDFDocumentPageOperationsHelper::rebindOcrLayers(document, PDFDocumentPageOperationsHelper::createDocument(builder, document), rotatedPages);
}

PDFDocument PDFDocumentPageOperations::removePages(const PDFDocument* document, const std::vector<PDFInteger>& pages, const PDFOperationControl* control, RemovalStatistics* statistics)
{
    if (!document)
    {
        throw PDFException(tr("There is no document."));
    }

    RemovalStatistics localStatistics;
    PDFDocumentPageRemoval removal(document, pages, control);
    PDFDocument result = removal.run(&localStatistics);
    if (statistics)
    {
        *statistics = localStatistics;
    }
    return result;
}

std::shared_ptr<const PDFDocumentPageOperations::PageIndex> PDFDocumentPageOperations::createPageIndex(const PDFDocument* document, const PDFOperationControl* control)
{
    if (!document)
    {
        throw PDFException(tr("There is no document."));
    }

    std::shared_ptr<PageIndex> index = std::make_shared<PageIndex>();
    const PDFCatalog* catalog = document->getCatalog();
    index->m_pageCount = catalog->getPageCount();

    std::vector<PDFObject> pageObjects;
    pageObjects.reserve(index->m_pageCount);
    for (size_t i = 0; i < index->m_pageCount; ++i)
    {
        const PDFPage* page = catalog->getPage(i);
        index->m_pages.insert(page->getPageReference());
        pageObjects.push_back(PDFObject::createReference(page->getPageReference()));
        for (const PDFObjectReference& annotation : page->getAnnotations())
        {
            index->m_annotations.insert(annotation);
        }
    }
    index->m_pages.erase(PDFObjectReference());
    index->m_annotations.erase(PDFObjectReference());
    index->m_hasRepeatedPage = !PDFDocumentPageOperationsHelper::getPageIndices(document, &index->m_pageIndices);
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return nullptr;
    }

    // The layers, which a page reaches (its content, resources, annotations - and, through
    // /Parent, the rest of the page tree, as the walk of removePages from the removed pages)
    PDFDocumentBuilder builder(document);
    const PDFDictionary* catalogDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference()));
    const PDFDictionary* properties = catalogDictionary ? builder.getDictionaryFromObject(catalogDictionary->get("OCProperties")) : nullptr;
    const PDFObject groups = properties ? builder.getObject(properties->get("OCGs")) : PDFObject();
    if (groups.isArray() && groups.getArray()->getCount() > 0)
    {
        const std::set<PDFObjectReference> reachable = PDFObjectUtils::getReferences(pageObjects, document->getStorage());
        for (const PDFObject& group : *groups.getArray())
        {
            if (group.isReference() && reachable.count(group.getReference()))
            {
                index->m_layersOfPages.insert(group.getReference());
            }
        }
    }
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return nullptr;
    }

    // The page tree flattened once for all parts
    builder.flattenPageTree();
    index->m_flattenedDocument = PDFDocumentPageOperationsHelper::createDocument(builder, document);
    return index;
}

PDFDocument PDFDocumentPageOperations::keepPages(const PDFDocument* document, const PageIndex& index, const std::vector<PDFInteger>& keptPages, const PDFOperationControl* control, RemovalStatistics* statistics)
{
    if (!document)
    {
        throw PDFException(tr("There is no document."));
    }
    if (index.m_pageCount != document->getCatalog()->getPageCount())
    {
        throw PDFException(tr("The index belongs to another document."));
    }

    RemovalStatistics localStatistics;
    PDFDocumentPageRemoval removal(document, &index, keptPages, control);
    PDFDocument result = removal.run(&localStatistics);
    if (statistics)
    {
        *statistics = localStatistics;
    }
    return result;
}

QString PDFDocumentPageOperations::formatPageNumbers(const std::vector<PDFInteger>& pageIndices, int maximumParts, const std::function<QString(PDFInteger)>& formatNumber)
{
    QStringList parts;
    for (size_t i = 0; i < pageIndices.size();)
    {
        if (int(parts.size()) >= qMax(1, maximumParts))
        {
            parts << QStringLiteral("…");
            break;
        }

        // Owner D3 (2026-11-10): a run of two pages is a range too ("13–14", 388:17167)
        size_t last = i;
        while (last + 1 < pageIndices.size() && pageIndices[last + 1] == pageIndices[last] + 1)
        {
            ++last;
        }
        const PDFInteger firstNumber = pageIndices[i] + 1;
        const PDFInteger lastNumber = pageIndices[last] + 1;
        const QString firstText = formatNumber ? formatNumber(firstNumber) : QString::number(firstNumber);
        parts << (last == i ? firstText : QStringLiteral("%1–%2").arg(firstText, formatNumber ? formatNumber(lastNumber) : QString::number(lastNumber)));
        i = last + 1;
    }
    return parts.join(QStringLiteral(", "));
}

}   // namespace pdf
