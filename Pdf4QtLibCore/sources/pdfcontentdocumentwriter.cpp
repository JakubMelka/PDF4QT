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

// Edit Content, stage E0, package P05 (document writer, tool, JSON): the writer of a snapshot
// into a document builder (GUI_overhaul/edit_tool/zadani_jadro.md 4.4, 4.6).
//
// A write has two parts, and the first one never touches the builder:
//
//   A  (0) the fingerprints of all containers of the snapshot against the document, and a
//      modified structure model against the structure of the document read again; the choice of
//      the containers, which are written (changed, not read-only, not skipped, not used by a
//      skipped page); the bytes of each of them (PDFContentStreamWriter) and their compressed
//      form; the check of a changed form without resources of its own against every place, which
//      draws it, and the resources it gets, when a name was added to it;
//      every check of the structure, the run of its writer on a private copy of the storage of
//      the builder included. A mismatch, an error or a cancellation ends the write here: the
//      builder is as the caller gave it.
//   B  (1) the new objects, which the written containers reach, and their real references in
//      place of the provisional ones; (2) the streams of the forms and of the patterns; (3) the
//      pages; (5) the structure; (6) the facts. No step of B can fail.
//
// The writer reads the immutable snapshot and the document only: it has no session, so a caller
// without an editor (a tool, which adds content to pages one after the other) gives it one small
// snapshot per page, each into the same builder.

#include "pdfcontentdocumentwriter.h"

#include "pdfcatalog.h"
#include "pdfcontentstructure.h"
#include "pdfcontenttreebuilder.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentpageoperations.h"
#include "pdfexception.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfpage.h"
#include "pdfprogress.h"
#include "pdfresourcewalk.h"
#include "pdfstreamfilters.h"
#include "pdfstructuretreewriter.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace pdf
{

/// Ends the part of the progress, which the writer has started, on every way out of a write.
///
/// Threads and ownership: a local object of one call; the sink is owned by the caller of the write.
class PDFContentDocumentWriterProgress
{
public:
    explicit PDFContentDocumentWriterProgress(PDFProgressSink* progress, size_t stepCount, const QString& text) :
        m_progress(progress)
    {
        if (m_progress)
        {
            ProgressStartupInfo info;
            info.showDialog = false;
            info.text = text;
            m_progress->start(stepCount, std::move(info));
        }
    }

    ~PDFContentDocumentWriterProgress()
    {
        if (m_progress)
        {
            m_progress->finish();
        }
    }

    PDFContentDocumentWriterProgress(const PDFContentDocumentWriterProgress&) = delete;
    PDFContentDocumentWriterProgress& operator=(const PDFContentDocumentWriterProgress&) = delete;

    void step()
    {
        if (m_progress)
        {
            m_progress->step();
        }
    }

private:
    PDFProgressSink* m_progress;
};

/// The parts of a write of a snapshot.
///
/// Threads and ownership: static functions and plain values of one call; nothing is kept.
class PDFContentDocumentWriterHelper
{
public:
    PDFContentDocumentWriterHelper() = delete;

    using Writer = PDFContentDocumentWriter;

    /// The key of the errors, which belong to no page (a form nothing uses, the structure)
    static constexpr PDFInteger NO_PAGE = -1;

    /// What a written stream of a form or of a pattern has under /Resources
    enum class ResourcesMode
    {
        Own,        ///< The resources of the container as a new dictionary (PDFContentResources::write)
        None,       ///< No entry: the container goes on using the resources of the page it is drawn on
        Inherited,  ///< A copy of the resources the container was built with (of its caller and of its page), with the added names
        Kept        ///< The entry of the document as it is (a value, which is no dictionary)
    };

    /// A container, which is written
    struct Item
    {
        PDFContentContainerPointer container;
        std::vector<PDFInteger> pages;          ///< A page: its index (and the other indices of the same page object); a form or a pattern: the pages, which use it; ascending
        PDFContentStreamWriteResult stream;     ///< The content and the resources
        QByteArray compressedContent;           ///< The content compressed by Flate
        ResourcesMode resourcesMode = ResourcesMode::Own;   ///< A form or a pattern: its /Resources
        PDFObject inheritedResources;           ///< ResourcesMode::Inherited: the resources the container was built with (PDFContentContainer::getInheritedResources, else the resource dictionary of its page; a dictionary, or null for a page without one)
    };

    /// A changed container, which is not written because of a skipped page
    struct LeftOutContainer
    {
        PDFContentContainerKey key;
        std::vector<PDFInteger> skippedPages;   ///< The skipped pages, which keep it out (a page: itself); ascending
    };

    static bool isPage(const PDFContentContainerKey& key) { return key.kind == PDFContentContainerKey::Kind::Page; }

    /// The entries of a stream dictionary, which describe its data (ISO 32000-2, 7.3.8.2, Table 5):
    /// a stream with new data does not keep them
    static bool isStreamDataKey(const QByteArray& key)
    {
        return key == "Length" || key == "Filter" || key == "DecodeParms" || key == "F" || key == "FFilter" || key == "FDecodeParms" || key == "DL";
    }

    /// A value is in the builder what it is in the document: the same value (the same reference
    /// or an equal direct object), and the object of a reference is an equal object
    static bool isSameValue(const PDFDocument* document, const PDFDocumentBuilder* builder, const PDFObject& current, const PDFObject& original)
    {
        return current == original && (!current.isReference() || builder->getObject(current) == document->getObject(original));
    }

    /// The content of a key is in the builder what it is in the document: /Contents of the page
    /// (the stream, or each stream of the array) and its /Resources are the same values, the
    /// stream of a form or of a pattern is an equal object. The fingerprint was computed over
    /// the document; this ties it to the builder.
    static bool isSourceUnchanged(const PDFDocument* document, const PDFDocumentBuilder* builder, const PDFContentContainerKey& key)
    {
        const PDFObject& current = builder->getObjectByReference(key.reference);
        const PDFObject& original = document->getObjectByReference(key.reference);

        if (!isPage(key))
        {
            return current.isStream() && current == original;
        }

        if (!current.isDictionary() || !original.isDictionary())
        {
            return false;
        }

        const PDFObject& currentContents = current.getDictionary()->get("Contents");
        const PDFObject& originalContents = original.getDictionary()->get("Contents");
        if (!isSameValue(document, builder, currentContents, originalContents) ||
            !isSameValue(document, builder, current.getDictionary()->get("Resources"), original.getDictionary()->get("Resources")))
        {
            return false;
        }

        // The streams of an array of /Contents (the array itself is equal already)
        const PDFObject& contentsArray = document->getObject(originalContents);
        if (contentsArray.isArray())
        {
            for (size_t i = 0; i < contentsArray.getArray()->getCount(); ++i)
            {
                const PDFObject& item = contentsArray.getArray()->getItem(i);
                if (!isSameValue(document, builder, item, item))
                {
                    return false;
                }
            }
        }

        return true;
    }

    /// A node of this kind is an object for the user (a text, a path, an image, a form, a shading)
    static bool isObjectKind(PDFContentNodeKind kind)
    {
        switch (kind)
        {
            case PDFContentNodeKind::TextObject:
            case PDFContentNodeKind::Path:
            case PDFContentNodeKind::Image:
            case PDFContentNodeKind::FormUse:
            case PDFContentNodeKind::Shading:
                return true;

            default:
                return false;
        }
    }

    /// The operation ends an object of the content: a path painting operator, Do, sh, an inline
    /// image, and BT for a text object (one object for BT ... ET)
    static bool isObjectOperator(PDFContentOperator contentOperator)
    {
        switch (contentOperator)
        {
            case PDFContentOperator::PathStroke:
            case PDFContentOperator::PathCloseStroke:
            case PDFContentOperator::PathFillWinding:
            case PDFContentOperator::PathFillWinding2:
            case PDFContentOperator::PathFillEvenOdd:
            case PDFContentOperator::PathFillStrokeWinding:
            case PDFContentOperator::PathFillStrokeEvenOdd:
            case PDFContentOperator::PathCloseFillStrokeWinding:
            case PDFContentOperator::PathCloseFillStrokeEvenOdd:
            case PDFContentOperator::PathClear:
            case PDFContentOperator::TextBegin:
            case PDFContentOperator::ShadingPaintShape:
            case PDFContentOperator::PaintXObject:
            case PDFContentOperator::InlineImage:
                return true;

            default:
                return false;
        }
    }

    /// What the object of one node subtree says about itself
    struct ObjectInfo
    {
        bool hasOriginal = false;   ///< An operation of the original content at its place belongs to it
        bool isEdited = false;      ///< An override, a canonical rewrite or a suppressed operation
        size_t firstOriginal = 0;   ///< The smallest index of its original operations
    };

    /// Marks the operations of a span as used and collects what they say about their object
    static void readSpan(const PDFContentOperationList& operations, const PDFContentOperationSpan& span, std::vector<bool>& used, ObjectInfo* info)
    {
        const size_t count = operations.getCount();
        const size_t originalCount = used.size();
        for (size_t i = 0; i < span.count; ++i)
        {
            const size_t index = span.first + i;
            if (index >= count)
            {
                break;
            }

            const PDFContentOperation::Flags flags = operations.getOperation(index).getFlags();
            if (index < originalCount)
            {
                used[index] = true;
            }

            if (!info)
            {
                continue;
            }

            if (index < originalCount && !flags.testFlag(PDFContentOperation::Generated))
            {
                info->firstOriginal = info->hasOriginal ? qMin(info->firstOriginal, index) : index;
                info->hasOriginal = true;
            }
            if (flags.testFlag(PDFContentOperation::Suppressed))
            {
                info->isEdited = true;
            }
        }
    }

    /// Number of the items, which are not in a longest strictly increasing subsequence: the
    /// smallest number of objects, which must have moved to give this order
    static PDFInteger countMoved(const std::vector<size_t>& sequence)
    {
        // Patience sorting: tails[k] is the smallest last item of an increasing subsequence of
        // the length k + 1, so the number of the tails is the length of the longest one
        std::vector<size_t> tails;
        for (const size_t item : sequence)
        {
            auto it = std::lower_bound(tails.begin(), tails.end(), item);
            if (it == tails.end())
            {
                tails.push_back(item);
            }
            else
            {
                *it = item;
            }
        }

        return PDFInteger(sequence.size() - tails.size());
    }

    /// Adds the objects of a written container to the facts: an object without an original
    /// operation is added; an object with an override, a canonical rewrite, a suppressed
    /// operation or at another place of the order is edited; an object ending operation of the
    /// original content, which no node names any more, is a removed object.
    static void countObjects(const PDFContentContainer& container, PDFContentWriteResult* result)
    {
        const PDFContentOperationList& operations = container.getOperations();
        std::vector<bool> used(operations.getOriginalCount(), false);

        // The first original operation of each object, which is not edited in another way, in the
        // order of the tree: the objects out of their order have moved. An edited object is
        // counted once, wherever it is.
        std::vector<size_t> order;

        // The tree is walked with a stack of its own (the depth of a tree is given by the document).
        // An object node is read with its whole subtree; object nodes are never nested.
        std::vector<const PDFContentNode*> stack;
        if (container.getRoot())
        {
            stack.push_back(container.getRoot().get());
        }

        std::vector<const PDFContentNode*> objectStack;
        while (!stack.empty())
        {
            const PDFContentNode* node = stack.back();
            stack.pop_back();

            if (!isObjectKind(node->getKind()))
            {
                readSpan(operations, node->getPrefix(), used, nullptr);
                readSpan(operations, node->getSuffix(), used, nullptr);
                const std::vector<PDFContentNodePointer>& children = node->getChildren();
                for (auto it = children.crbegin(); it != children.crend(); ++it)
                {
                    if (*it)
                    {
                        stack.push_back(it->get());
                    }
                }
                continue;
            }

            ObjectInfo info;
            objectStack.clear();
            objectStack.push_back(node);
            while (!objectStack.empty())
            {
                const PDFContentNode* part = objectStack.back();
                objectStack.pop_back();

                readSpan(operations, part->getPrefix(), used, &info);
                readSpan(operations, part->getSuffix(), used, &info);
                if (part->getOverride() || part->isModified())
                {
                    info.isEdited = true;
                }

                for (const PDFContentNodePointer& child : part->getChildren())
                {
                    if (child)
                    {
                        objectStack.push_back(child.get());
                    }
                }
            }

            if (!info.hasOriginal)
            {
                ++result->objectsAdded;
                continue;
            }

            if (info.isEdited)
            {
                ++result->objectsEdited;
            }
            else
            {
                order.push_back(info.firstOriginal);
            }
        }

        result->objectsEdited += countMoved(order);

        for (size_t i = 0; i < used.size(); ++i)
        {
            if (!used[i] && isObjectOperator(operations.getOperation(i).getOperator()))
            {
                ++result->objectsRemoved;
            }
        }
    }

    /// The reference is a provisional one of a new object of the snapshot: it has the reserved
    /// generation of the session (no object in use of a document has it, ISO 32000-2, 7.5.4) and
    /// it is a key of the new objects. A reference of the document - also one to an object,
    /// which does not exist, with the number of a new object - is never one.
    static bool isProvisional(PDFObjectReference reference, const std::map<PDFObjectReference, PDFObject>& newObjects)
    {
        return reference.generation == PDFContentEditSession::NEW_OBJECT_GENERATION && newObjects.count(reference) > 0;
    }

    /// The provisional references of new objects, which an object refers to directly
    static void collectNewReferences(const PDFObject& object, const std::map<PDFObjectReference, PDFObject>& newObjects, std::set<PDFObjectReference>& reached, std::vector<PDFObjectReference>& queue)
    {
        for (const PDFObjectReference reference : PDFObjectUtils::getDirectReferences(object))
        {
            if (isProvisional(reference, newObjects) && reached.insert(reference).second)
            {
                queue.push_back(reference);
            }
        }
    }

    /// Step (1): adds the new objects, which the written containers reach, to the builder and
    /// replaces their provisional references by the real ones - in the entries, which the
    /// editor has added to the resources, and in the new objects themselves; nowhere else. An
    /// entry of the document (an original one) and every other object of the builder are never
    /// touched: only the values of the snapshot hold provisional references.
    /// \returns the number of the new fonts (the new objects, which an added entry of /Font names)
    static PDFInteger writeNewObjects(const PDFContentEditSnapshot& snapshot, PDFDocumentBuilder* builder, std::vector<Item>& items)
    {
        if (snapshot.newObjects.empty())
        {
            return 0;
        }

        // One visited set for the whole walk over the new objects; the queue holds each of them once
        std::set<PDFObjectReference> reached;
        std::vector<PDFObjectReference> queue;
        std::set<PDFObjectReference> fonts;
        for (const Item& item : items)
        {
            for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
            {
                for (const PDFContentResources::Entry& entry : item.stream.resources.getEntries(category))
                {
                    if (entry.isOriginal)
                    {
                        continue;
                    }

                    collectNewReferences(entry.object, snapshot.newObjects, reached, queue);
                    if (category == PDFContentResourceCategory::Font && entry.object.isReference() && isProvisional(entry.object.getReference(), snapshot.newObjects))
                    {
                        fonts.insert(entry.object.getReference());
                    }
                }
            }
        }

        for (size_t i = 0; i < queue.size(); ++i)
        {
            // A copy: the queue grows
            const PDFObjectReference reference = queue[i];
            collectNewReferences(snapshot.newObjects.at(reference), snapshot.newObjects, reached, queue);
        }

        if (reached.empty())
        {
            return 0;
        }

        // The real references first (in the order of the provisional ones), then the objects with
        // all references replaced at once: the new objects refer to each other
        std::map<PDFObjectReference, PDFObjectReference> mapping;
        for (const PDFObjectReference reference : reached)
        {
            mapping[reference] = builder->addObject(snapshot.newObjects.at(reference));
        }

        for (const auto& mapped : mapping)
        {
            builder->setObject(mapped.second, PDFObjectUtils::replaceReferences(snapshot.newObjects.at(mapped.first), mapping));
        }

        for (Item& item : items)
        {
            for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
            {
                // A copy: setObject changes the entries
                const std::vector<PDFContentResources::Entry> entries = item.stream.resources.getEntries(category);
                for (const PDFContentResources::Entry& entry : entries)
                {
                    if (!entry.isOriginal)
                    {
                        const PDFObject replaced = PDFObjectUtils::replaceReferences(entry.object, mapping);
                        if (replaced != entry.object)
                        {
                            item.stream.resources.setObject(category, entry.name, replaced);
                        }
                    }
                }
            }
        }

        return PDFInteger(fonts.size());
    }

    /// The dictionary of a new content stream
    /// \param original The dictionary of the stream in the document, nullptr for a new stream
    /// \param length The length of the new data
    /// \param isResourcesKept The entry /Resources of the original is taken over
    static PDFDictionaryBuilder createStreamDictionary(const PDFDictionary* original, qsizetype length, bool isResourcesKept)
    {
        PDFDictionaryBuilder dictionary;
        if (original)
        {
            for (size_t i = 0; i < original->getCount(); ++i)
            {
                const QByteArray key = original->getKey(i).getString();
                if (!isStreamDataKey(key) && (isResourcesKept || key != "Resources"))
                {
                    dictionary.addEntry(original->getKey(i), original->getValue(i));
                }
            }
        }

        dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(length));
        dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
        return dictionary;
    }

    /// The object is a content, which inherits its resources, because it has none of its own
    /// (ISO 32000-2, 7.8.3): a form of /XObject, a tiling pattern of /Pattern or a Type 3 font of
    /// /Font, whose /Resources is missing, null or a reference to nothing. The processor of this
    /// library gives such a content the names of the content, which draws it, and a name that
    /// content does not define from the page (NOTES.txt 7.8.3).
    static bool isInheritingContent(const PDFDocument* document, PDFContentResourceCategory category, const PDFObject& object)
    {
        const PDFDictionary* dictionary = nullptr;
        switch (category)
        {
            case PDFContentResourceCategory::XObject:
                if (object.isStream() && document->getObject(object.getStream()->getDictionary()->get("Subtype")) == PDFObject::createName("Form"))
                {
                    dictionary = object.getStream()->getDictionary();
                }
                break;

            case PDFContentResourceCategory::Pattern:
                if (object.isStream())
                {
                    dictionary = object.getStream()->getDictionary();
                }
                break;

            case PDFContentResourceCategory::Font:
                if (object.isDictionary() && document->getObject(object.getDictionary()->get("Subtype")) == PDFObject::createName("Type3"))
                {
                    dictionary = object.getDictionary();
                }
                break;

            default:
                break;
        }

        return dictionary && document->getObject(dictionary->get("Resources")).isNull();
    }

    /// The names of the category dictionaries of resources with their values, each dictionary
    /// indexed once per container: a lookup costs log n, not a pass over the dictionary. The
    /// dictionaries belong to the document or to a container of the snapshot, which outlive it.
    using NameIndex = std::map<const PDFDictionary*, std::map<QByteArray, PDFObject>>;

    /// The value resources give a name of a category: the first entry of the key with a value
    /// other than null (ISO 32000-2, 7.3.7: an entry with the null value is no entry - so the
    /// processor merges the dictionaries of content without resources); null, when there is none
    static PDFObject getIndexedResource(const PDFDocument* document, const PDFDictionary* resources, const PDFContentResourceName& name, NameIndex& index)
    {
        const PDFDictionary* category = resources ? document->getDictionaryFromObject(resources->get(PDFContentResources::getCategoryKey(name.category))) : nullptr;
        if (!category)
        {
            return PDFObject();
        }

        auto it = index.find(category);
        if (it == index.end())
        {
            std::map<QByteArray, PDFObject> names;
            for (size_t i = 0; i < category->getCount(); ++i)
            {
                if (!category->getValue(i).isNull())
                {
                    names.emplace(category->getKey(i).getString(), category->getValue(i));
                }
            }
            it = index.emplace(category, std::move(names)).first;
        }

        auto itName = it->second.find(name.name);
        return itName != it->second.cend() ? itName->second : PDFObject();
    }

    /// Adds every name of every category of the resources (with a value other than null)
    static void addResourceNames(const PDFDocument* document, const PDFDictionary* resources, std::set<PDFContentResourceName>& names)
    {
        for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
        {
            const PDFDictionary* categoryDictionary = resources ? document->getDictionaryFromObject(resources->get(PDFContentResources::getCategoryKey(category))) : nullptr;
            for (size_t i = 0; categoryDictionary && i < categoryDictionary->getCount(); ++i)
            {
                if (!categoryDictionary->getValue(i).isNull())
                {
                    names.insert(PDFContentResourceName{ category, categoryDictionary->getKey(i).getString() });
                }
            }
        }
    }

    /// Two values of a name give a content the same object: the same value, or equal objects
    /// behind two references (null and null too: neither defines the name)
    static bool isSameResource(const PDFDocument* document, const PDFObject& left, const PDFObject& right)
    {
        return left == right || document->getObject(left) == document->getObject(right);
    }

    /// The resources of a page give every name of \p names the object the resources of the
    /// build give it; an added name (one of \p addedNames) they must not define at all
    static bool isSamePageResources(const PDFDocument* document,
                                    const PDFDictionary* page,
                                    const PDFDictionary* built,
                                    const std::set<PDFContentResourceName>& names,
                                    const std::set<PDFContentResourceName>& addedNames,
                                    NameIndex& index)
    {
        for (const PDFContentResourceName& name : names)
        {
            const PDFObject pageObject = getIndexedResource(document, page, name, index);
            if (addedNames.count(name) ? !pageObject.isNull() : !isSameResource(document, pageObject, getIndexedResource(document, built, name, index)))
            {
                return false;
            }
        }
        return true;
    }

    /// The name of the resource, which set a property of a state (PDFContentPropertySource)
    static void addSourceName(const PDFContentPropertySource& source, PDFContentStateProperty property, std::set<PDFContentResourceName>& names)
    {
        switch (source.origin)
        {
            case PDFContentPropertySource::Origin::Inherited:
                break;

            case PDFContentPropertySource::Origin::ExtGState:
                if (!source.resourceName.isEmpty())
                {
                    names.insert(PDFContentResourceName{ PDFContentResourceCategory::ExtGState, source.resourceName });
                }
                break;

            case PDFContentPropertySource::Origin::Operator:
                if (property == PDFContentStateProperty::Font && !source.resourceName.isEmpty())
                {
                    names.insert(PDFContentResourceName{ PDFContentResourceCategory::Font, source.resourceName });
                }
                else if (property == PDFContentStateProperty::StrokeColor || property == PDFContentStateProperty::FillColor)
                {
                    if (!source.resourceName.isEmpty())
                    {
                        names.insert(PDFContentResourceName{ PDFContentResourceCategory::ColorSpace, source.resourceName });
                    }
                    if (!source.patternName.isEmpty())
                    {
                        names.insert(PDFContentResourceName{ PDFContentResourceCategory::Pattern, source.patternName });
                    }
                }
                break;
        }
    }

    /// The names, whose objects the build resolved into values, which the written content can
    /// keep without naming them (review R2-2): an override writes a property by its value (R-2:
    /// a moved node keeps the state of its old place; R-3: a residual state run sets the leak of
    /// a node, which no longer reaches the nodes behind it - makeValue of the command engine).
    /// For every property of every override of the tree: the resource, which set it in the state
    /// at the beginning of the node and in the state at the end of its leak. More names than
    /// needed (a value the user chose has a source too), never fewer.
    static void addBakedResourceNames(const PDFContentContainer& container, std::set<PDFContentResourceName>& names)
    {
        // The tree is walked with a stack of its own (its depth is given by the document)
        std::vector<const PDFContentNode*> stack;
        if (container.getRoot())
        {
            stack.push_back(container.getRoot().get());
        }
        while (!stack.empty())
        {
            const PDFContentNode* node = stack.back();
            stack.pop_back();

            if (const PDFContentStateOverride* override = node->getOverride())
            {
                for (const auto& property : override->properties)
                {
                    if (node->getSymbolicState())
                    {
                        addSourceName(node->getSymbolicState()->getSource(property.first), property.first, names);
                    }
                    if (node->getLeak().endSymbolicState)
                    {
                        addSourceName(node->getLeak().endSymbolicState->getSource(property.first), property.first, names);
                    }
                }
            }

            for (const PDFContentNodePointer& child : node->getChildren())
            {
                if (child)
                {
                    stack.push_back(child.get());
                }
            }
        }
    }

    /// The contents with resources of their own, in which the container of the walk node
    /// \p walkNode is drawn (review R2-1). The container has no resources of its own: it takes a
    /// name from the content drawing it and a name that content lacks from the page (NOTES.txt
    /// 7.8.3); content without resources drawing it passes the resources of its own caller on.
    /// So the container is drawn in the context of an owner with resources of its own Q (a page,
    /// a form, a pattern, a Type 3 font, an appearance stream, a soft mask group) and of the page
    /// P under it, when Q names it (an edge of the walk), or when Q names content without
    /// resources and Q or P names the container - that content can draw it through any chain of
    /// such content (each of them has the same Q and P). The walk has no edges from content
    /// without resources, so the second kind is taken conservatively: every owner with resources
    /// of its own, which names content without resources and which is reachable from a page
    /// naming the container (a page, which does not name it, can give it to no such chain; a Q,
    /// which names it, is of the first kind). More owners than draw it, never fewer.
    ///
    /// Bounds: one pass over the nodes and their edges, one breadth-first walk with one visited
    /// set (the walk itself is bounded by its depth and its budget of nodes and edges).
    /// \param pageNodes The page nodes among them
    /// \param otherNodes The other owners
    static void collectDrawingOwners(const PDFResourceWalkResult& walk, size_t walkNode, std::vector<size_t>* pageNodes, std::vector<size_t>* otherNodes)
    {
        const size_t count = walk.nodes.size();
        std::vector<bool> isDrawing(count, false);
        std::vector<bool> isReached(count, false);
        std::vector<size_t> queue;

        // The first kind: the owners naming the container; the pages among them start the walk
        for (size_t i = 0; i < count; ++i)
        {
            const std::vector<size_t>& children = walk.nodes[i].children;
            if (std::find(children.cbegin(), children.cend(), walkNode) != children.cend())
            {
                isDrawing[i] = true;
                if (walk.nodes[i].kind == PDFResourceOwnerKind::Page)
                {
                    isReached[i] = true;
                    queue.push_back(i);
                }
            }
        }

        for (size_t i = 0; i < queue.size(); ++i)
        {
            for (const size_t child : walk.nodes[queue[i]].children)
            {
                if (child < count && !isReached[child])
                {
                    isReached[child] = true;
                    queue.push_back(child);
                }
            }
        }

        // The second kind: an owner with resources of its own reached from such a page, which
        // names content without resources
        for (size_t i = 0; i < count; ++i)
        {
            const PDFResourceWalkNode& node = walk.nodes[i];
            if (isDrawing[i] || !isReached[i] || node.resources.isNull())
            {
                continue;
            }

            for (const size_t child : node.children)
            {
                if (child < count && walk.nodes[child].kind != PDFResourceOwnerKind::Page && walk.nodes[child].resources.isNull())
                {
                    isDrawing[i] = true;
                    break;
                }
            }
        }

        for (size_t i = 0; i < count; ++i)
        {
            if (isDrawing[i] && i != walkNode)
            {
                (walk.nodes[i].kind == PDFResourceOwnerKind::Page ? pageNodes : otherNodes)->push_back(i);
            }
        }
    }

    /// The resource names of the operations PDFContentStreamWriter writes for a container: the
    /// operations of the nodes of its tree (the prefix and the suffix of each node), without the
    /// suppressed ones. The operation list of a container only grows - the operations of a
    /// deleted node stay in it, but no node of the tree names them and they are not written. (The
    /// writer replaces the prefix of a path, whose geometry was changed, by the canonical text of
    /// the path; the operations of a path name no resource, so it is taken as it is.) The names
    /// the stream writer writes for overrides are names it adds to the resources of the container
    /// (PDFContentStreamWriteResult::resources).
    static std::set<PDFContentResourceName> getWrittenResourceNames(const PDFContentContainer& container)
    {
        std::set<PDFContentResourceName> names;
        const PDFContentOperationList& operations = container.getOperations();
        const size_t count = operations.getCount();

        const auto addSpan = [&names, &operations, count](const PDFContentOperationSpan& span)
        {
            if (span.first > count || span.count > count - span.first)
            {
                // The stream writer refuses the container then
                return;
            }

            for (size_t index = span.first; index < span.first + span.count; ++index)
            {
                const PDFContentOperation& operation = operations.getOperation(index);
                if (!operation.getFlags().testFlag(PDFContentOperation::Suppressed))
                {
                    names.insert(operation.getResourceNames().cbegin(), operation.getResourceNames().cend());
                }
            }
        };

        // The tree is walked with a stack of its own (its depth is given by the document)
        std::vector<const PDFContentNode*> stack;
        if (container.getRoot())
        {
            stack.push_back(container.getRoot().get());
        }
        while (!stack.empty())
        {
            const PDFContentNode* node = stack.back();
            stack.pop_back();

            addSpan(node->getData().prefix);
            addSpan(node->getSuffix());

            for (const PDFContentNodePointer& child : node->getChildren())
            {
                if (child)
                {
                    stack.push_back(child.get());
                }
            }
        }

        return names;
    }

    /// A changed form or pattern without /Resources of its own (no entry, null, a reference to
    /// nothing): what its stream gets under /Resources, or the error, which refuses it. The
    /// guarantee: no page changes its look silently.
    ///
    /// Such a container is drawn with the resources of each place, which draws it: a name from
    /// the content drawing it, a name that content lacks from the page (NOTES.txt 7.8.3; the
    /// contexts: collectDrawingOwners). It was built with the resources of one of them (the
    /// recorded page, PDFContentContainer::getInheritedResourcesPageIndex, and the resources of
    /// the build, getInheritedResources: the merged ones of its caller, or those of the page).
    /// Its tree holds the objects of these resources, and the commands keep some of them by value
    /// (R-2, R-3: addBakedResourceNames). So the written content is right only where every other
    /// context resolves every name it uses (and every baked name) to the same object as the
    /// resources of the build - also when no name was added (review R2-2). Checked:
    ///  - the recorded page is one of the pages, which reach the container (else the build had
    ///    resources, with which the document does not draw it);
    ///  - every other context gives each such name the same object (the same value, or equal
    ///    objects behind two references); a name of the content, which a context does not define,
    ///    is looked up in each page reaching the container (more pages than needed, never fewer);
    ///    the added names are left out (the container defines them itself).
    /// When a name was added (R-4), the container gets a copy of the resources of the build with
    /// the added names in copies of their categories: the resources of its own, which every
    /// context then uses. When the container also draws a form, a pattern or a Type 3 font
    /// without resources of its own (also a new object of the editor), that content, whose names
    /// nobody knows here, takes its names from the copy after the write and a name the copy
    /// lacks from the page, so the check is strict: every name of the resources of the build is
    /// compared in every context, a content drawing the container may define no name the
    /// resources of the build do not, and no context may define an added name (the builder
    /// reserves the names of the resources of the build, so the allocator does not give them).
    /// Without a page, which draws it, the container inherits nothing: it gets a dictionary of
    /// the added names, or stays without one.
    /// \param walkNode The index of the node of the container in the walk (the size of the nodes, when it has none)
    /// \param isResourcesChanged A name was added to the container
    /// \returns the error, which refuses the container; empty, when item.resourcesMode and
    ///          item.inheritedResources are set
    static QString prepareInheritedResources(const PDFDocument* document,
                                             const PDFContentEditSnapshot& snapshot,
                                             const PDFResourceWalkResult& walk,
                                             size_t walkNode,
                                             const PDFOperationControl* control,
                                             bool isResourcesChanged,
                                             Item& item)
    {
        if (item.pages.empty())
        {
            item.resourcesMode = isResourcesChanged ? ResourcesMode::Own : ResourcesMode::None;
            return QString();
        }

        if (walk.isLimitReached)
        {
            return Writer::tr("The content has no resources of its own and the document has too many nested resources to find every place, which draws it: its change cannot be written.");
        }

        const PDFCatalog* catalog = document->getCatalog();
        const PDFInteger recordedPageIndex = item.container->getInheritedResourcesPageIndex();
        if (recordedPageIndex < 0)
        {
            // The tree builder took the entry /Resources for resources of the container (a
            // reference to an object, which does not exist): its content was read without the
            // resources of any page
            return Writer::tr("The content has no resources of its own and was not read with the resources of a page: its change cannot be written.");
        }
        if (!std::binary_search(item.pages.cbegin(), item.pages.cend(), recordedPageIndex) || size_t(recordedPageIndex) >= catalog->getPageCount())
        {
            return Writer::tr("The content has no resources of its own and the page, whose resources it was read with, does not draw it: its change cannot be written.");
        }

        // The resources of the build: the merged ones of the caller, or those of the page
        const PDFObject recordedResources = document->getObject(catalog->getPage(size_t(recordedPageIndex))->getResources());
        const PDFObject& builtValue = item.container->getInheritedResources();
        const PDFObject builtResources = builtValue.isDictionary() ? builtValue : recordedResources;
        const PDFDictionary* built = builtResources.isDictionary() ? builtResources.getDictionary() : nullptr;

        // The names the written content uses or keeps by value and does not define itself, and
        // whether it draws a content, which inherits its resources too
        const PDFContentResources& resources = item.stream.resources;
        std::set<PDFContentResourceName> names = getWrittenResourceNames(*item.container);
        addBakedResourceNames(*item.container, names);
        std::set<PDFContentResourceName> checkedNames;
        for (const PDFContentResourceName& name : names)
        {
            if (resources.getObject(name.category, name.name).isNull())
            {
                checkedNames.insert(name);
            }
        }

        NameIndex index;
        bool isStrict = false;
        if (isResourcesChanged)
        {
            for (const PDFContentResourceName& name : checkedNames)
            {
                isStrict = isStrict || isInheritingContent(document, name.category, document->getObject(getIndexedResource(document, built, name, index)));
            }

            for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
            {
                for (const PDFContentResources::Entry& entry : resources.getEntries(category))
                {
                    const bool isNewObject = entry.object.isReference() && isProvisional(entry.object.getReference(), snapshot.newObjects);
                    const PDFObject object = isNewObject ? snapshot.newObjects.at(entry.object.getReference()) : document->getObject(entry.object);
                    isStrict = isStrict || isInheritingContent(document, category, object);
                }
            }
        }

        // Strict: every name of the resources of the build, and the names added to the copy, which
        // are no names of them (the copy keeps them all) and which no context may define
        std::set<PDFContentResourceName> addedNames;
        if (isStrict)
        {
            addResourceNames(document, built, checkedNames);

            for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
            {
                for (const PDFContentResources::Entry& entry : resources.getEntries(category))
                {
                    if (!entry.isOriginal)
                    {
                        if (!getIndexedResource(document, built, PDFContentResourceName{ category, entry.name }, index).isNull())
                        {
                            return Writer::tr("The content has no resources of its own and draws other content without resources: the new resource “%1” would hide a resource of the same name from it. A resource cannot be added to it.").arg(QString::fromLatin1(entry.name));
                        }
                        addedNames.insert(PDFContentResourceName{ category, entry.name });
                    }
                }
            }
        }

        std::vector<size_t> pageNodes;
        std::vector<size_t> otherNodes;
        collectDrawingOwners(walk, walkNode, &pageNodes, &otherNodes);

        // The owners with resources of their own drawing the container: their names; a name of
        // the content they do not define is looked up in the pages
        std::set<PDFContentResourceName> namesOfPages;
        QString callerError;
        for (const size_t nodeIndex : otherNodes)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                // The caller asks the control after every container
                return QString();
            }

            const PDFResourceWalkNode& node = walk.nodes[nodeIndex];
            const PDFDictionary* caller = node.resources.isDictionary() ? node.resources.getDictionary() : nullptr;
            bool isDifferent = false;
            for (const PDFContentResourceName& name : checkedNames)
            {
                const PDFObject callerObject = getIndexedResource(document, caller, name, index);
                if (callerObject.isNull())
                {
                    namesOfPages.insert(name);
                }
                else if (!isSameResource(document, callerObject, getIndexedResource(document, built, name, index)))
                {
                    isDifferent = true;
                }
            }

            if (isStrict)
            {
                std::set<PDFContentResourceName> callerNames;
                addResourceNames(document, caller, callerNames);
                for (const PDFContentResourceName& name : callerNames)
                {
                    // (an added name is none of the built ones: a caller defining it differs too)
                    isDifferent = isDifferent || getIndexedResource(document, built, name, index).isNull();
                }

                // Content without resources drawn by the container takes an added name, which
                // the caller does not define, from a page now and from the copy after the write
                namesOfPages.insert(addedNames.cbegin(), addedNames.cend());
            }

            if (isDifferent && callerError.isEmpty())
            {
                callerError = Writer::tr("The content has no resources of its own and is drawn with the resources of each place, which uses it. Its change cannot be written, because a form or an annotation of the page %1, which draws it, gives it other resources than those it was read with.")
                        .arg(node.pageIndex + 1);
            }
        }

        // The pages naming the container are contexts of their own: every checked name
        std::set<PDFInteger> drawingPages;
        for (const auto& [pageIndex, pageNode] : walk.pages)
        {
            if (std::find(pageNodes.cbegin(), pageNodes.cend(), pageNode) != pageNodes.cend())
            {
                drawingPages.insert(pageIndex);
            }
        }

        // The pages (each resource dictionary once - the pages of a document often share one)
        std::vector<PDFInteger> differentPages;
        std::map<std::pair<const PDFDictionary*, bool>, bool> comparedPages;
        for (const PDFInteger pageIndex : item.pages)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                return QString();
            }

            if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
            {
                continue;
            }

            const bool isDrawingPage = drawingPages.count(pageIndex) > 0;
            const PDFObject pageResources = document->getObject(catalog->getPage(size_t(pageIndex))->getResources());
            const PDFDictionary* page = pageResources.isDictionary() ? pageResources.getDictionary() : nullptr;
            auto it = comparedPages.find(std::make_pair(page, isDrawingPage));
            if (it == comparedPages.end())
            {
                // A page naming the container is a context of its own (every checked name, and no
                // added name); every page reaching it gives the names a caller does not define
                const bool isSame = isSamePageResources(document, page, built, namesOfPages, addedNames, index) &&
                                    (!isDrawingPage || (isSamePageResources(document, page, built, checkedNames, addedNames, index) &&
                                                        isSamePageResources(document, page, built, addedNames, addedNames, index)));
                it = comparedPages.emplace(std::make_pair(page, isDrawingPage), isSame).first;
            }

            if (!it->second)
            {
                differentPages.push_back(pageIndex);
            }
        }

        if (!differentPages.empty())
        {
            return Writer::tr("The content has no resources of its own and is drawn with the resources of each place, which uses it. Its change cannot be written, because the pages %1 give it other resources than the page %2, with which it was read.")
                    .arg(PDFDocumentPageOperations::formatPageNumbers(differentPages)).arg(recordedPageIndex + 1);
        }

        if (!callerError.isEmpty())
        {
            return callerError;
        }

        if (isResourcesChanged)
        {
            item.resourcesMode = ResourcesMode::Inherited;
            item.inheritedResources = built ? builtResources : PDFObject();
        }
        else
        {
            item.resourcesMode = ResourcesMode::None;
        }
        return QString();
    }

    /// What the written stream of a form or of a pattern has under /Resources (part A: nothing
    /// is written). Its own resources are the dictionary, which the entry of its stream gives in
    /// the builder (the source was compared with the document: isSourceUnchanged). A missing
    /// entry, a null value and a reference to an object, which does not exist, are no resources
    /// (ISO 32000-2, 7.3.7: "a dictionary entry whose value is null shall be treated the same as
    /// if the entry does not exist"; 7.3.10: a reference to an undefined object is the null
    /// object): the container inherits the resources of the place drawing it, as the tree builder
    /// and the renderer read it (prepareInheritedResources). A value of another type gives the
    /// content no resources at all in this library: it is kept, until a name is added.
    /// \returns the error, which refuses the container; empty otherwise
    static QString prepareStreamResources(const PDFDocument* document,
                                          const PDFDocumentBuilder* builder,
                                          const PDFContentEditSnapshot& snapshot,
                                          const PDFResourceWalkResult& walk,
                                          size_t walkNode,
                                          const PDFOperationControl* control,
                                          Item& item)
    {
        const PDFObject current = builder->getObjectByReference(item.container->getKey().reference);
        const PDFObject ownResources = builder->getObject(current.getStream()->getDictionary()->get("Resources"));

        if (ownResources.isDictionary())
        {
            item.resourcesMode = ResourcesMode::Own;
            return QString();
        }

        if (!ownResources.isNull())
        {
            item.resourcesMode = item.stream.resources.isChanged() ? ResourcesMode::Own : ResourcesMode::Kept;
            return QString();
        }

        if (item.container->isUnchanged())
        {
            // A forced rewrite of an unchanged container (Options::forceRewrite): the bytes of
            // the original streams, nothing resolved by the build is written
            item.resourcesMode = ResourcesMode::None;
            return QString();
        }

        // Without resources of its own: a changed content is checked against every place drawing
        // it, also when no name was added (review R2-2)
        return prepareInheritedResources(document, snapshot, walk, walkNode, control, item.stream.resources.isChanged(), item);
    }

    // ---- The structure ----

    /// The object of a reference is in the builder what it is in the document. An object, which
    /// was compared already, is not compared again.
    static bool isSameObject(const PDFDocument* document, const PDFDocumentBuilder* builder, PDFObjectReference reference, std::set<PDFObjectReference>& compared)
    {
        return !compared.insert(reference).second || builder->getObjectByReference(reference) == document->getObjectByReference(reference);
    }

    /// The structure of the builder is the structure of the document: the entry /StructTreeRoot
    /// of the catalog, the object of every element of the model read from the document (the root
    /// is its first element) and the nodes of /ParentTree with the arrays they refer to are equal
    /// objects. The first
    /// write of a structure into a builder changes at least one of them (the root gets a new
    /// parent tree, or a changed element a new dictionary), so a second one is refused here, as a
    /// second write of a container is by isSourceUnchanged.
    ///
    /// The walk over the nodes of the parent tree has one visited set (a node reached twice or
    /// a node, which is its own descendant, is entered once) and the depth limit of the model.
    /// \param structure A structure model of \p document (its source fingerprint is the one of the document)
    static bool isStructureSourceUnchanged(const PDFDocument* document, const PDFDocumentBuilder* builder, const PDFContentStructureModel& structure)
    {
        const PDFDictionary* trailer = document->getTrailerDictionary();
        const PDFDictionary* catalog = trailer ? document->getDictionaryFromObject(trailer->get("Root")) : nullptr;
        const PDFObject builderCatalog = builder->getObjectByReference(builder->getCatalogReference());
        if (!catalog || !builderCatalog.isDictionary())
        {
            return false;
        }

        const PDFObject& rootValue = catalog->get("StructTreeRoot");
        if (builderCatalog.getDictionary()->get("StructTreeRoot") != rootValue)
        {
            return false;
        }

        // The elements; the first one is the root (a root, which is no object of its own, is
        // the entry of the catalog compared above)
        std::set<PDFObjectReference> compared;
        for (const PDFContentStructureElement& element : structure.getElements())
        {
            if (element.reference.isValid() && !isSameObject(document, builder, element.reference, compared))
            {
                return false;
            }
        }

        const PDFDictionary* root = document->getDictionaryFromObject(rootValue);
        if (!root)
        {
            return true;
        }

        // The nodes of the number tree (ISO 32000-2, 7.9.7): /Kids are nodes, the odd items of
        // /Nums the values (an array of the elements of a page, or an element)
        std::set<PDFObjectReference> visited;
        std::vector<std::pair<PDFObject, int>> stack;
        stack.emplace_back(root->get("ParentTree"), 0);
        while (!stack.empty())
        {
            const std::pair<PDFObject, int> current = std::move(stack.back());
            stack.pop_back();

            if (current.first.isReference())
            {
                if (!visited.insert(current.first.getReference()).second)
                {
                    continue;
                }
                if (!isSameObject(document, builder, current.first.getReference(), compared))
                {
                    return false;
                }
            }

            const PDFDictionary* node = document->getDictionaryFromObject(current.first);
            if (!node)
            {
                continue;
            }

            for (const char* key : { "Nums", "Kids" })
            {
                const PDFObject& value = node->get(key);
                if (value.isReference() && !isSameObject(document, builder, value.getReference(), compared))
                {
                    return false;
                }

                const PDFObject& items = document->getObject(value);
                if (!items.isArray())
                {
                    continue;
                }

                const bool isKids = key[0] == 'K';
                const PDFArray* array = items.getArray();
                for (size_t i = isKids ? 0 : 1; i < array->getCount(); i += isKids ? 1 : 2)
                {
                    const PDFObject& item = array->getItem(i);
                    if (isKids)
                    {
                        if (current.second < PDFContentStructureModel::MAXIMUM_NUMBER_TREE_DEPTH)
                        {
                            stack.emplace_back(item, current.second + 1);
                        }
                    }
                    else if (item.isReference() && !isSameObject(document, builder, item.getReference(), compared))
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    /// The copy of the resources of the build for a container without resources of its own, with
    /// the added names in copies of their categories (an added name wins over a name of those
    /// resources, which the content does not use; never with content without resources inside:
    /// prepareInheritedResources)
    static PDFObject createInheritedResources(const PDFDocument* document, const Item& item)
    {
        const PDFDictionary* inherited = item.inheritedResources.isDictionary() ? item.inheritedResources.getDictionary() : nullptr;

        PDFDictionaryBuilder resources = inherited ? PDFDictionaryBuilder(*inherited) : PDFDictionaryBuilder();
        for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
        {
            const QByteArray categoryKey = PDFContentResources::getCategoryKey(category);
            const PDFDictionary* inheritedCategory = inherited ? document->getDictionaryFromObject(inherited->get(categoryKey)) : nullptr;
            PDFDictionaryBuilder categoryDictionary = inheritedCategory ? PDFDictionaryBuilder(*inheritedCategory) : PDFDictionaryBuilder();

            bool isAdded = false;
            for (const PDFContentResources::Entry& entry : item.stream.resources.getEntries(category))
            {
                if (!entry.isOriginal)
                {
                    categoryDictionary.setEntry(PDFInplaceOrMemoryString(entry.name), entry.object);
                    isAdded = true;
                }
            }

            if (isAdded)
            {
                resources.setEntry(PDFInplaceOrMemoryString(categoryKey), PDFObject::createDictionary(std::move(categoryDictionary)));
            }
        }

        return PDFObject::createDictionary(std::move(resources));
    }

    /// Step (2): the stream of a form or of a pattern gets its new data at its own object
    static void writeStreamContainer(const PDFDocument* document, PDFDocumentBuilder* builder, Item& item)
    {
        const PDFObjectReference reference = item.container->getKey().reference;
        const PDFObject current = builder->getObjectByReference(reference);
        const PDFDictionary* original = current.getStream()->getDictionary();

        // An entry, which gives no resources (null, a reference to nothing), is not taken over:
        // it says what a missing entry says, and an object added later could get the number of
        // such a reference
        PDFDictionaryBuilder dictionary = createStreamDictionary(original, item.compressedContent.size(), item.resourcesMode == ResourcesMode::Kept);
        switch (item.resourcesMode)
        {
            case ResourcesMode::Own:
                // Its own dictionary: a dictionary shared with another content is never changed in place
                dictionary.setEntry(PDFInplaceOrMemoryString("Resources"), item.stream.resources.write(builder));
                break;

            case ResourcesMode::Inherited:
                dictionary.setEntry(PDFInplaceOrMemoryString("Resources"), createInheritedResources(document, item));
                break;

            case ResourcesMode::None:
            case ResourcesMode::Kept:
                break;
        }

        builder->setObject(reference, PDFObject::createStream(PDFStream(std::move(dictionary), std::move(item.compressedContent))));
    }

    /// Step (3): a page gets one new content stream in place of its whole /Contents and its own
    /// /Resources dictionary; the old streams and a shared resource dictionary stay as they are
    static void writePage(PDFDocumentBuilder* builder, Item& item)
    {
        const PDFObjectReference reference = item.container->getKey().reference;
        const PDFObject current = builder->getObjectByReference(reference);

        PDFDictionaryBuilder streamDictionary = createStreamDictionary(nullptr, item.compressedContent.size(), false);
        const PDFObjectReference contents = builder->addObject(PDFObject::createStream(PDFStream(std::move(streamDictionary), std::move(item.compressedContent))));

        PDFDictionaryBuilder page(*current.getDictionary());
        page.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createReference(contents));
        page.setEntry(PDFInplaceOrMemoryString("Resources"), item.stream.resources.write(builder));
        builder->setObject(reference, PDFObject::createDictionary(std::move(page)));
    }
};

PDFContentWriteResult PDFContentDocumentWriter::write(const PDFDocument* document,
                                                      const PDFContentEditSnapshot& snapshot,
                                                      PDFDocumentBuilder* builder,
                                                      const Options& options,
                                                      const PDFOperationControl* control,
                                                      PDFProgressSink* progress)
{
    using Helper = PDFContentDocumentWriterHelper;

    PDFContentWriteResult result;

    if (!builder)
    {
        result.status = PDFContentWriteResult::Status::PageErrors;
        result.errors[Helper::NO_PAGE].push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId(), tr("There is no document to write into.") });
        return result;
    }

    // One step for the fingerprint and one for the bytes of each container
    PDFContentDocumentWriterProgress progressGuard(progress, 2 * snapshot.containers.size(), tr("Writing the content of the pages"));

    // ---- (0) The fingerprints: the identifiers of the nodes of a container are valid only for
    //      the content it was built from (RE-15) ----
    for (const auto& item : snapshot.containers)
    {
        const PDFContentContainerPointer& container = item.second;
        const QByteArray fingerprint = (container && container->getKey() == item.first) ? PDFContentTreeBuilder::computeFingerprint(document, item.first) : QByteArray();
        if (fingerprint.isEmpty() || fingerprint != container->getFingerprint())
        {
            result.changedContainers.push_back(item.first);
        }

        progressGuard.step();
        if (PDFOperationControl::isOperationCanceled(control))
        {
            result.status = PDFContentWriteResult::Status::Canceled;
            result.changedContainers.clear();
            return result;
        }
    }

    if (!result.changedContainers.empty() || !document)
    {
        result.status = PDFContentWriteResult::Status::DocumentChanged;
        return result;
    }

    // The structure model is not covered by a fingerprint of a container: a modified one is
    // written whole (its changed elements and the whole parent tree), so it must be the
    // structure of this document. The model keeps the fingerprint of everything its reading
    // took from the document (PDFContentStructureModel::getSourceFingerprint), also of the parts
    // it has changed since; the document gives it now.
    const bool isStructureWritten = snapshot.structure && !snapshot.structure->isEmpty() && snapshot.structure->isModified();
    if (isStructureWritten)
    {
        const QByteArray fingerprint = PDFContentStructureModel::computeSourceFingerprint(document, control);
        if (PDFOperationControl::isOperationCanceled(control))
        {
            result.status = PDFContentWriteResult::Status::Canceled;
            return result;
        }

        if (fingerprint.isEmpty() || fingerprint != snapshot.structure->getSourceFingerprint())
        {
            result.status = PDFContentWriteResult::Status::DocumentChanged;
            result.isStructureOutdated = true;
            return result;
        }
    }

    // ---- The containers, which are written: forms and patterns first, then the pages ----
    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());

    std::vector<Helper::Item> items;

    bool hasStreamContainer = false;
    for (const auto& item : snapshot.containers)
    {
        const PDFContentContainer& container = *item.second;
        if (!Helper::isPage(item.first) && !container.isReadOnly() && (options.forceRewrite || !container.isUnchanged()))
        {
            hasStreamContainer = true;
        }
    }

    // The pages, which use a form or a pattern: one walk over the resources of all pages, and
    // only when a form or a pattern is written (a snapshot of pages alone needs no walk)
    PDFResourceWalkResult walk;
    std::map<PDFObjectReference, size_t> walkNodes;
    if (hasStreamContainer)
    {
        std::vector<PDFInteger> pageIndices(size_t(pageCount), 0);
        for (PDFInteger i = 0; i < pageCount; ++i)
        {
            pageIndices[size_t(i)] = i;
        }

        walk = PDFResourceWalk::walk(document, pageIndices, PDFResourceWalk::Options(), control);
        if (walk.isCanceled || PDFOperationControl::isOperationCanceled(control))
        {
            result.status = PDFContentWriteResult::Status::Canceled;
            return result;
        }

        for (size_t i = 0; i < walk.nodes.size(); ++i)
        {
            if (walk.nodes[i].reference.isValid() && walk.nodes[i].kind != PDFResourceOwnerKind::Page)
            {
                walkNodes.emplace(walk.nodes[i].reference, i);
            }
        }
    }

    // A skipped page stays as it is: its own container is left out, and so is a form or a
    // pattern, which a skipped page uses - also when a written page uses it too (the form is one
    // object: writing it would change the skipped page)
    std::vector<Helper::LeftOutContainer> leftOutContainers;
    std::map<PDFContentContainerKey, size_t> itemWalkNodes;
    for (const bool isPagePass : { false, true })
    {
        for (const auto& snapshotItem : snapshot.containers)
        {
            const PDFContentContainerKey& key = snapshotItem.first;
            const PDFContentContainerPointer& container = snapshotItem.second;
            if (Helper::isPage(key) != isPagePass || container->isReadOnly() || (!options.forceRewrite && container->isUnchanged()))
            {
                continue;
            }

            Helper::Item item;
            item.container = container;

            std::vector<PDFInteger> skippedPages;
            if (isPagePass)
            {
                // A page object is in the page tree once (the reader refuses a tree, which lists
                // a page twice), so the page of the key is the only page changed
                item.pages = { key.pageIndex };
            }
            else
            {
                auto it = walkNodes.find(key.reference);
                if (it != walkNodes.cend())
                {
                    item.pages = walk.getPageIndices(it->second);
                    itemWalkNodes[key] = it->second;
                }
            }

            std::copy_if(item.pages.cbegin(), item.pages.cend(), std::back_inserter(skippedPages), [&options](PDFInteger pageIndex) { return options.skippedPages.count(pageIndex) > 0; });
            if (!isPagePass && skippedPages.empty() && walk.isLimitReached)
            {
                // The walk was stopped by its limits (owners nested too deep, too many of
                // them): it does not know every page of the container, so any skipped page
                // may be one of them
                skippedPages.assign(options.skippedPages.cbegin(), options.skippedPages.cend());
            }

            if (!skippedPages.empty())
            {
                if (!isPagePass)
                {
                    result.skippedContainers[key] = skippedPages;
                }
                leftOutContainers.push_back(Helper::LeftOutContainer{ key, std::move(skippedPages) });
                continue;
            }

            items.push_back(std::move(item));
        }
    }

    // ---- The bytes of the written containers; nothing is written with an error ----
    for (Helper::Item& item : items)
    {
        const PDFContentContainerKey& key = item.container->getKey();
        std::vector<PDFContentWriteError> errors;

        // The content in the builder must still be the content of the document, whose fingerprint
        // was compared: a builder of another document, and a builder, into which the container was
        // written already (a second snapshot of the same page in one run), are refused
        if (!Helper::isSourceUnchanged(document, builder, key))
        {
            errors.push_back(PDFContentWriteError{ key, PDFContentNodeId(), tr("The content was changed in the document, which is being written.") });
        }
        else
        {
            item.stream = PDFContentStreamWriter::write(*item.container);
            errors = std::move(item.stream.errors);
            item.stream.errors.clear();

            if (errors.empty() && !Helper::isPage(key))
            {
                // What the stream gets under /Resources; a form without resources of its own,
                // which got a name, can be refused here
                auto it = itemWalkNodes.find(key);
                const QString resourcesError = Helper::prepareStreamResources(document, builder, snapshot, walk, it != itemWalkNodes.cend() ? it->second : walk.nodes.size(), control, item);
                if (!resourcesError.isEmpty())
                {
                    errors.push_back(PDFContentWriteError{ key, PDFContentNodeId(), resourcesError });
                }
            }

            if (errors.empty())
            {
                try
                {
                    item.compressedContent = PDFFlateDecodeFilter::compress(item.stream.content);
                }
                catch (const PDFException& exception)
                {
                    errors.push_back(PDFContentWriteError{ key, PDFContentNodeId(), exception.getMessage() });
                }
            }
        }

        if (!errors.empty())
        {
            // An error of a page is the error of the page; an error of a form or of a pattern is
            // listed under every page, which uses it (none of them is skipped: the container
            // would have been left out)
            std::vector<PDFInteger> errorPages = item.pages;
            if (errorPages.empty())
            {
                errorPages.push_back(Helper::NO_PAGE);
            }

            for (PDFContentWriteError& error : errors)
            {
                error.container = key;
            }
            for (const PDFInteger pageIndex : errorPages)
            {
                std::vector<PDFContentWriteError>& pageErrors = result.errors[pageIndex];
                pageErrors.insert(pageErrors.end(), errors.cbegin(), errors.cend());
            }
        }

        progressGuard.step();
        if (PDFOperationControl::isOperationCanceled(control))
        {
            result = PDFContentWriteResult();
            result.status = PDFContentWriteResult::Status::Canceled;
            return result;
        }
    }

    // ---- The structure: every check before anything is written ----
    PDFContentStructureModelPointer structure = snapshot.structure;
    if (isStructureWritten)
    {
        // A container left out (a skipped page, a form of a skipped page) keeps the tags of
        // its content as the document has them: the changes of its key (/StructParents) are
        // taken out of the model, the changes of the written containers stay; an element with
        // changed children of both is taken apart child by child. Refused, where the two cannot
        // be separated (PDFContentStructureModel::withoutChangesOfKeys; the error names the
        // skipped page), and for a key of a left-out container, which a written container has
        // too, with a change.
        std::set<PDFInteger> leftOutKeys;
        for (const Helper::LeftOutContainer& leftOut : leftOutContainers)
        {
            auto it = snapshot.containers.find(leftOut.key);
            const PDFInteger key = it != snapshot.containers.cend() ? it->second->getStructuralParentKey() : -1;
            if (key >= 0)
            {
                leftOutKeys.insert(key);
            }
        }

        QString separationError;
        if (!leftOutKeys.empty())
        {
            const PDFContentStructureModelPointer original = PDFContentStructureModel::read(document, control);
            if (PDFOperationControl::isOperationCanceled(control))
            {
                result = PDFContentWriteResult();
                result.status = PDFContentWriteResult::Status::Canceled;
                return result;
            }

            structure = original ? snapshot.structure->withoutChangesOfKeys(*original, leftOutKeys, &separationError) : nullptr;
            for (const Helper::Item& item : items)
            {
                // The key of a written container, which is left out too, with a change of its tags
                const PDFInteger key = item.container->getStructuralParentKey();
                if (structure && original && leftOutKeys.count(key))
                {
                    const PDFContentStructureModelPointer withoutKey = snapshot.structure->withoutChangesOfKeys(*original, { key }, nullptr);
                    if (!withoutKey || !withoutKey->isEqual(*snapshot.structure))
                    {
                        structure = nullptr;
                        separationError = tr("The tags of this page and of a written content have one key: they cannot be left out alone. Undo the changes of the tags of the page instead.");
                    }
                }
            }

            if (!structure)
            {
                if (separationError.isEmpty())
                {
                    separationError = tr("The tags of the page cannot be left out alone. Undo the changes of the tags of the page instead.");
                }
                for (const Helper::LeftOutContainer& leftOut : leftOutContainers)
                {
                    for (const PDFInteger pageIndex : leftOut.skippedPages)
                    {
                        result.errors[pageIndex].push_back(PDFContentWriteError{ leftOut.key, PDFContentNodeId(), separationError });
                    }
                }
            }
        }

        if (structure && !Helper::isStructureSourceUnchanged(document, builder, *structure))
        {
            result.errors[Helper::NO_PAGE].push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId(), tr("The tags were changed in the document, which is being written.") });
        }
        else if (structure)
        {
            // All checks of the writer of the structure run here, where nothing is written yet.
            // The steps (1) - (3) touch no object of the structure, so the real run of the step
            // (5) gives the same answer.
            const QString error = PDFStructureTreeWriter::check(builder, *structure);
            if (!error.isEmpty())
            {
                result.errors[Helper::NO_PAGE].push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId(), error });
            }
        }

        if (PDFOperationControl::isOperationCanceled(control))
        {
            result = PDFContentWriteResult();
            result.status = PDFContentWriteResult::Status::Canceled;
            return result;
        }
    }

    if (!result.errors.empty())
    {
        result.status = PDFContentWriteResult::Status::PageErrors;
        result.skippedContainers.clear();
        return result;
    }

    // ---- (1) The new objects, before anything else adds an object to the builder ----
    result.fontsAdded = Helper::writeNewObjects(snapshot, builder, items);

    // ---- (2) Forms and patterns, (3) pages; (6) the facts of each of them ----
    std::set<PDFInteger> pagesChanged;
    for (Helper::Item& item : items)
    {
        Helper::countObjects(*item.container, &result);
        pagesChanged.insert(item.pages.cbegin(), item.pages.cend());
        result.isPagesChangedIncomplete = result.isPagesChangedIncomplete || (!Helper::isPage(item.container->getKey()) && walk.isLimitReached);

        if (Helper::isPage(item.container->getKey()))
        {
            Helper::writePage(builder, item);
        }
        else
        {
            Helper::writeStreamContainer(document, builder, item);
        }
    }
    result.pagesChanged.assign(pagesChanged.cbegin(), pagesChanged.cend());

    // ---- (4) Layers: from stage E4 ----

    // ---- (5) The structure: once, after the new objects of the snapshot (the writer of the
    //      structure adds objects to the builder). Its checks have passed on the copy of the
    //      builder in part A, and the steps above have touched no object of the structure. ----
    if (isStructureWritten && structure)
    {
        PDFStructureTreeWriteResult structureResult = PDFStructureTreeWriter::write(builder, *structure);
        Q_ASSERT(structureResult.errors.empty());
        for (QString& error : structureResult.errors)
        {
            // Not reachable (see above); an error is never dropped silently
            result.status = PDFContentWriteResult::Status::PageErrors;
            result.errors[Helper::NO_PAGE].push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId(), std::move(error) });
        }
        result.isStructureChanged = structureResult.isWritten;
    }

    return result;
}

PDFDocumentPointer PDFContentDocumentWriter::writePagePreview(const PDFDocument* document,
                                                              const PDFContentEditSnapshot& snapshot,
                                                              PDFInteger pageIndex,
                                                              const PDFOperationControl* control,
                                                              PDFContentWriteResult* result)
{
    PDFContentWriteResult dummy;
    PDFContentWriteResult& writeResult = result ? *result : dummy;
    writeResult = PDFContentWriteResult();

    if (!document || pageIndex < 0 || size_t(pageIndex) >= document->getCatalog()->getPageCount())
    {
        writeResult.status = PDFContentWriteResult::Status::PageErrors;
        writeResult.errors[pageIndex].push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId(), tr("The page does not exist in the document.") });
        return PDFDocumentPointer();
    }

    // The containers of the page: its own one and the forms and patterns its resources reach in
    // the document, and those, which the changed page names in the entries the editor has added
    PDFContentEditSnapshot pageSnapshot;
    pageSnapshot.revision = snapshot.revision;
    pageSnapshot.newObjects = snapshot.newObjects;

    std::set<PDFObjectReference> reached;
    PDFResourceWalk::Options walkOptions;
    walkOptions.includeAnnotations = false;
    const PDFResourceWalkResult walk = PDFResourceWalk::walk(document, { pageIndex }, walkOptions, control);
    for (const PDFResourceWalkNode& node : walk.nodes)
    {
        if (node.reference.isValid() && node.kind != PDFResourceOwnerKind::Page)
        {
            reached.insert(node.reference);
        }
    }

    for (const auto& item : snapshot.containers)
    {
        if (item.first.kind == PDFContentContainerKey::Kind::Page && item.first.pageIndex == pageIndex && item.second)
        {
            pageSnapshot.containers.insert(item);
            for (const PDFContentResourceCategory category : { PDFContentResourceCategory::XObject, PDFContentResourceCategory::Pattern })
            {
                for (const PDFContentResources::Entry& entry : item.second->getResources().getEntries(category))
                {
                    if (!entry.isOriginal && entry.object.isReference())
                    {
                        reached.insert(entry.object.getReference());
                    }
                }
            }
        }
    }

    for (const auto& item : snapshot.containers)
    {
        if (item.first.kind != PDFContentContainerKey::Kind::Page && reached.count(item.first.reference))
        {
            pageSnapshot.containers.insert(item);
        }
    }

    if (walk.isCanceled || PDFOperationControl::isOperationCanceled(control))
    {
        writeResult.status = PDFContentWriteResult::Status::Canceled;
        return PDFDocumentPointer();
    }

    PDFDocumentBuilder builder(document);
    writeResult = write(document, pageSnapshot, &builder, Options(), control, nullptr);
    if (!writeResult.isSuccess())
    {
        return PDFDocumentPointer();
    }

    try
    {
        return PDFDocumentPointer(new PDFDocument(builder.build()));
    }
    catch (const PDFException& exception)
    {
        writeResult.status = PDFContentWriteResult::Status::PageErrors;
        writeResult.errors[pageIndex].push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId(), exception.getMessage() });
        return PDFDocumentPointer();
    }
}

}   // namespace pdf
