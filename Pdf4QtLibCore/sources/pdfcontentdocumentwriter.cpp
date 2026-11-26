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
//      form; the resources a form without resources of its own gets, when a name was added to it;
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
        Inherited,  ///< A copy of the resources of the page the container was built with, with the added names
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
        PDFObject inheritedResources;           ///< ResourcesMode::Inherited: the resource dictionary of the page (a dictionary, or null for a page without one)
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

    /// The value a resource dictionary gives a name of a category; null, when it defines none
    static PDFObject getResource(const PDFDocument* document, const PDFDictionary* resources, PDFContentResourceCategory category, const QByteArray& name)
    {
        const PDFDictionary* categoryDictionary = resources ? document->getDictionaryFromObject(resources->get(PDFContentResources::getCategoryKey(category))) : nullptr;
        return categoryDictionary ? categoryDictionary->get(name) : PDFObject();
    }

    /// The object is a content, which uses the resources of the page it is drawn on, because it
    /// has none of its own (ISO 32000-2, 7.8.3): a form of /XObject, a tiling pattern of
    /// /Pattern or a Type 3 font of /Font, whose /Resources is missing, null or a reference to
    /// nothing. The renderer of this library gives such a content the dictionaries of the
    /// content, which draws it.
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

    /// Two resource dictionaries give a content the same objects.
    /// \param recorded The dictionary of the page the content was built with, nullptr = none
    /// \param otherResources The other resources, dereferenced (anything else than a dictionary = none)
    /// \param usedNames The names the content uses
    /// \param isStrict Every category must be equal as a whole (a category without a name is as a
    ///        missing one); otherwise each used name must be the same value, or an equal object
    ///        behind another reference
    static bool isSameResources(const PDFDocument* document,
                                const PDFDictionary* recorded,
                                const PDFObject& otherResources,
                                const std::set<PDFContentResourceName>& usedNames,
                                bool isStrict)
    {
        const PDFDictionary* other = otherResources.isDictionary() ? otherResources.getDictionary() : nullptr;

        if (isStrict)
        {
            for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
            {
                const QByteArray categoryKey = PDFContentResources::getCategoryKey(category);
                const PDFDictionary* recordedCategory = recorded ? document->getDictionaryFromObject(recorded->get(categoryKey)) : nullptr;
                const PDFDictionary* otherCategory = other ? document->getDictionaryFromObject(other->get(categoryKey)) : nullptr;
                const bool isRecordedEmpty = !recordedCategory || recordedCategory->getCount() == 0;
                const bool isOtherEmpty = !otherCategory || otherCategory->getCount() == 0;
                if (isRecordedEmpty != isOtherEmpty || (!isRecordedEmpty && !(*recordedCategory == *otherCategory)))
                {
                    return false;
                }
            }

            return true;
        }

        for (const PDFContentResourceName& name : usedNames)
        {
            const PDFObject recordedObject = getResource(document, recorded, name.category, name.name);
            const PDFObject otherObject = getResource(document, other, name.category, name.name);
            if (recordedObject != otherObject && document->getObject(recordedObject) != document->getObject(otherObject))
            {
                return false;
            }
        }

        return true;
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

    /// The resources for a form or a pattern without /Resources of its own, to which the editor
    /// has added names (R-4): the resource dictionary of the page the container was built with
    /// (PDFContentContainer::getInheritedResourcesPageIndex) - writeStreamContainer copies it and
    /// puts the added names into copies of their categories.
    ///
    /// The copy takes the place of the resources of every page, which draws the container
    /// (ISO 32000-2, 7.8.3), so it is right only when all those pages agree with that page:
    ///  - the recorded page is one of the pages, which reach the container (the container was
    ///    built with resources, with which the document really draws it);
    ///  - every other such page gives every name the operations of the container use the same
    ///    object (the same value, or equal objects behind two references), category by category;
    ///    the added names are left out (the container defines them itself, and the allocator of
    ///    the names avoids every name the content uses);
    ///  - the same holds for the resources of every other content, which names the container in
    ///    its own resources (a form or an appearance stream with a resource dictionary, which
    ///    draws the container): this library draws the container with the dictionaries of the
    ///    content, which draws it, so the copy takes the place of those resources too;
    ///  - when the content draws a form, a pattern or a Type 3 font without resources of its own
    ///    (also a new object of the editor), that content sees the copy in place of the resources
    ///    of the page in this library, and nobody knows here, which names it uses: every category
    ///    of the other resources must then be equal as a whole, and no added name may take the
    ///    place of a name of the page.
    /// A container no page draws inherits nothing: it gets a dictionary of the added names.
    /// \param walkNode The index of the node of the container in the walk (the size of the nodes, when it has none)
    /// \returns the error, which refuses the container; empty, when item.resourcesMode and
    ///          item.inheritedResources are set
    static QString prepareInheritedResources(const PDFDocument* document,
                                             const PDFContentEditSnapshot& snapshot,
                                             const PDFResourceWalkResult& walk,
                                             size_t walkNode,
                                             const PDFOperationControl* control,
                                             Item& item)
    {
        if (walk.isLimitReached)
        {
            return Writer::tr("The document has too many nested resources to find every page, which draws this content: a resource cannot be added to it.");
        }

        if (item.pages.empty())
        {
            item.resourcesMode = ResourcesMode::Own;
            return QString();
        }

        const PDFCatalog* catalog = document->getCatalog();
        const PDFInteger recordedPageIndex = item.container->getInheritedResourcesPageIndex();
        if (recordedPageIndex < 0)
        {
            // The tree builder took the entry /Resources for resources of the container (a
            // reference to an object, which does not exist): its content was read without the
            // resources of any page
            return Writer::tr("The content has no resources of its own and was not read with the resources of a page: a resource cannot be added to it.");
        }
        if (!std::binary_search(item.pages.cbegin(), item.pages.cend(), recordedPageIndex) || size_t(recordedPageIndex) >= catalog->getPageCount())
        {
            return Writer::tr("The content has no resources of its own and the page, whose resources it was read with, does not draw it: a resource cannot be added to it.");
        }

        const PDFObject& recordedValue = catalog->getPage(size_t(recordedPageIndex))->getResources();
        const PDFObject recordedResources = document->getObject(recordedValue);
        const PDFDictionary* recorded = recordedResources.isDictionary() ? recordedResources.getDictionary() : nullptr;

        // The names the written content uses and does not define itself, and whether it draws a
        // content, which inherits its resources too
        const PDFContentResources& resources = item.stream.resources;
        std::set<PDFContentResourceName> usedNames;
        for (const PDFContentResourceName& name : getWrittenResourceNames(*item.container))
        {
            if (resources.getObject(name.category, name.name).isNull())
            {
                usedNames.insert(name);
            }
        }

        bool isStrict = false;
        for (const PDFContentResourceName& name : usedNames)
        {
            isStrict = isStrict || isInheritingContent(document, name.category, document->getObject(getResource(document, recorded, name.category, name.name)));
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

        if (isStrict)
        {
            for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
            {
                for (const PDFContentResources::Entry& entry : resources.getEntries(category))
                {
                    if (!getResource(document, recorded, category, entry.name).isNull())
                    {
                        return Writer::tr("The content has no resources of its own and draws other content without resources: the new resource “%1” would hide a resource of the page from it. A resource cannot be added to it.").arg(QString::fromLatin1(entry.name));
                    }
                }
            }
        }

        // The pages, which draw the content with other resources than the recorded page
        std::vector<PDFInteger> differentPages;
        for (const PDFInteger pageIndex : item.pages)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                // The caller asks the control after every container
                return QString();
            }

            if (pageIndex == recordedPageIndex || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
            {
                continue;
            }

            // The same dictionary object, or an equal direct one (the pages of a document often share one)
            const PDFObject& pageValue = catalog->getPage(size_t(pageIndex))->getResources();
            if (pageValue != recordedValue && !isSameResources(document, recorded, document->getObject(pageValue), usedNames, isStrict))
            {
                differentPages.push_back(pageIndex);
            }
        }

        if (!differentPages.empty())
        {
            return Writer::tr("The content has no resources of its own and is drawn with the resources of each page, which uses it. A resource cannot be added to it, because the pages %1 give it other resources than the page %2.")
                    .arg(PDFDocumentPageOperations::formatPageNumbers(differentPages)).arg(recordedPageIndex + 1);
        }

        // The other contents, which name the container in resources of their own
        for (size_t i = 0; i < walk.nodes.size(); ++i)
        {
            const PDFResourceWalkNode& node = walk.nodes[i];
            if (node.kind == PDFResourceOwnerKind::Page || std::find(node.children.cbegin(), node.children.cend(), walkNode) == node.children.cend())
            {
                continue;
            }

            if (PDFOperationControl::isOperationCanceled(control))
            {
                return QString();
            }

            if (!isSameResources(document, recorded, node.resources, usedNames, isStrict))
            {
                return Writer::tr("The content has no resources of its own and is drawn with the resources of the content, which uses it. A resource cannot be added to it, because a form or an annotation of the page %1 gives it other resources than the page %2.")
                        .arg(node.pageIndex + 1).arg(recordedPageIndex + 1);
            }
        }

        item.resourcesMode = ResourcesMode::Inherited;
        item.inheritedResources = recordedResources.isDictionary() ? recordedResources : PDFObject();
        return QString();
    }

    /// What the written stream of a form or of a pattern has under /Resources (part A: nothing
    /// is written). Its own resources are the dictionary, which the entry of its stream gives in
    /// the builder (the source was compared with the document: isSourceUnchanged). A missing
    /// entry, a null value and a reference to an object, which does not exist, are no resources
    /// (ISO 32000-2, 7.3.7: "a dictionary entry whose value is null shall be treated the same as
    /// if the entry does not exist"; 7.3.10: a reference to an undefined object is the null
    /// object): the container uses the resources of the page, as the tree builder and the
    /// renderer read it. A value of another type gives the content no resources at all in this
    /// library: it is kept, until a name is added.
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

        if (!item.stream.resources.isChanged())
        {
            item.resourcesMode = ownResources.isNull() ? ResourcesMode::None : ResourcesMode::Kept;
            return QString();
        }

        if (!ownResources.isNull())
        {
            item.resourcesMode = ResourcesMode::Own;
            return QString();
        }

        return prepareInheritedResources(document, snapshot, walk, walkNode, control, item);
    }

    // ---- The structure ----

    static bool isSameChild(const PDFContentStructureChild& left, const PDFContentStructureChild& right)
    {
        return left.kind == right.kind && left.element == right.element && left.mcid == right.mcid && left.page == right.page &&
               left.stream == right.stream && left.streamOwner == right.streamOwner && left.object == right.object;
    }

    /// An element, which the editor has not changed, is what the document gives now
    static bool isSameElement(const PDFContentStructureElement& left, const PDFContentStructureElement& right)
    {
        return left.id == right.id && left.parent == right.parent && left.type == right.type && left.standardType == right.standardType &&
               left.title == right.title && left.alternateText == right.alternateText && left.actualText == right.actualText &&
               left.expandedText == right.expandedText && left.language == right.language && left.elementId == right.elementId &&
               left.page == right.page && left.otherEntries == right.otherEntries &&
               std::equal(left.children.cbegin(), left.children.cend(), right.children.cbegin(), right.children.cend(), &isSameChild);
    }

    /// The structure model of a snapshot was read from the document: it agrees with the structure
    /// read again (\p current) in everything a model keeps from its reading - the elements with
    /// their objects in their order; every field of an element the editor has not changed (of a
    /// changed one its object and the keys the editor never changes); the keys of the parent
    /// tree with their original values; the content of every item the editor has not changed.
    /// What a model does not keep - the original children and texts of a changed element, the
    /// original array of a changed item, when the array is an object of its own - cannot be
    /// compared here.
    static bool isStructureOfDocument(const PDFContentStructureModel& model, const PDFContentStructureModel& current)
    {
        const std::vector<PDFContentStructureElement>& elements = model.getElements();
        const std::vector<PDFContentStructureElement>& currentElements = current.getElements();
        if (elements.size() < currentElements.size())
        {
            return false;
        }

        for (size_t i = 0; i < elements.size(); ++i)
        {
            const PDFContentStructureElement& element = elements[i];
            if (i >= currentElements.size())
            {
                // Only an element the editor has created is not in the document
                if (element.reference.isValid())
                {
                    return false;
                }
                continue;
            }

            const PDFContentStructureElement& currentElement = currentElements[i];
            if (element.reference != currentElement.reference || element.otherEntries != currentElement.otherEntries ||
                (!element.isModified && !isSameElement(element, currentElement)))
            {
                return false;
            }
        }

        const std::map<PDFInteger, PDFContentParentTreeItem>& parentTree = model.getParentTree();
        const std::map<PDFInteger, PDFContentParentTreeItem>& currentParentTree = current.getParentTree();
        for (const auto& item : parentTree)
        {
            auto it = currentParentTree.find(item.first);
            if (it == currentParentTree.cend())
            {
                // Only a key the editor has added is not in the document
                if (!item.second.isModified)
                {
                    return false;
                }
                continue;
            }

            if (item.second.raw != it->second.raw ||
                (!item.second.isModified && (item.second.kind != it->second.kind || item.second.elements != it->second.elements)))
            {
                return false;
            }
        }

        // A key the document has got since the model was read. (/ParentTreeNextKey and the
        // reference of the parent tree need no comparison of their own: they are among the
        // other entries of the root, the element 0, which were compared above.)
        for (const auto& item : currentParentTree)
        {
            if (!parentTree.count(item.first))
            {
                return false;
            }
        }

        return true;
    }

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
    /// \param structure The structure of \p document (read from it now)
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

    /// The copy of the resources of the page for a container without resources of its own, with
    /// the added names in copies of their categories (an added name wins over a name of the
    /// page, which the content does not use: prepareInheritedResources)
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
    // structure of this document. The structure is read again and compared with everything the
    // model keeps from its own reading.
    const bool isStructureWritten = snapshot.structure && !snapshot.structure->isEmpty() && snapshot.structure->isModified();
    PDFContentStructureModelPointer currentStructure;
    if (isStructureWritten)
    {
        currentStructure = PDFContentStructureModel::read(document, control);
        if (PDFOperationControl::isOperationCanceled(control))
        {
            result.status = PDFContentWriteResult::Status::Canceled;
            return result;
        }

        if (!currentStructure || !Helper::isStructureOfDocument(*snapshot.structure, *currentStructure))
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
    if (isStructureWritten)
    {
        // The model is written whole, and nothing tells, which of its changes belong to which
        // page (a deleted sequence, whose children do not agree with the parent tree, changes
        // elements only). With a changed container left out, the tags of content, which stays
        // as it is, would be written changed: the page cannot be skipped.
        for (const Helper::LeftOutContainer& leftOut : leftOutContainers)
        {
            for (const PDFInteger pageIndex : leftOut.skippedPages)
            {
                result.errors[pageIndex].push_back(PDFContentWriteError{ leftOut.key, PDFContentNodeId(), tr("The tags of the document were changed with its content and are written as a whole: the page cannot be left out. Undo the changes of the page instead.") });
            }
        }

        if (!Helper::isStructureSourceUnchanged(document, builder, *currentStructure))
        {
            result.errors[Helper::NO_PAGE].push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId(), tr("The tags were changed in the document, which is being written.") });
        }
        else
        {
            // The writer of the structure on a copy of the objects of the builder: all its
            // checks run here, where nothing is written yet. The steps (1) - (3) touch no
            // object of the structure, so the real run of the step (5) gives the same answer.
            PDFDocumentBuilder probe(*builder->getStorage(), document->getInfo()->version);
            PDFStructureTreeWriteResult probeResult = PDFStructureTreeWriter::write(&probe, *snapshot.structure);
            for (QString& error : probeResult.errors)
            {
                result.errors[Helper::NO_PAGE].push_back(PDFContentWriteError{ PDFContentContainerKey(), PDFContentNodeId(), std::move(error) });
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
    if (isStructureWritten)
    {
        PDFStructureTreeWriteResult structureResult = PDFStructureTreeWriter::write(builder, *snapshot.structure);
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
