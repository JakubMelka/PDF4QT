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

#include "pdfresourcewalk.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfoperationcontrol.h"
#include "pdfpage.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <set>

namespace pdf
{

/// The state of one walk: the visited set (PDFObjectWalk), the map of the entered owners to their
/// nodes and the queue of the nodes, whose resources are not read yet.
///
/// Review RS7CORE (2026-11-17): a resource dictionary (or one of its categories) shared by many owners
/// is read once per owner. So (1) a direct owner (a Type 3 font written directly into a shared /Font
/// dictionary) is entered once by its address in the immutable storage, as a reference is - entered
/// each time its shared container was read, two direct fonts, whose /Resources lead back to their own
/// /Font dictionary, doubled the nodes with each level up to the budget of 16 Mi nodes (gigabytes from a
/// file of a few hundred bytes); (2) the owners of a category dictionary are found once (a cache by
/// the dictionary), a node only adds its edges to them; (3) the edges have the budget of the nodes:
/// N owners, which share a dictionary of M owners, have N * M edges, a file of a few megabytes would
/// make billions of them. A walk over the budget is not complete (isLimitReached).
class PDFResourceWalkState
{
public:
    explicit PDFResourceWalkState(const PDFDocument* document, const PDFResourceWalk::Options& options) :
        document(document),
        walk(options.maximumDepth, options.maximumNodes),
        maximumEdges(options.maximumNodes)
    {

    }

    /// An owner of resources of a category dictionary: the owner object and its /Resources, both in the
    /// immutable storage of the document
    class Candidate
    {
    public:
        const PDFObject* owner = nullptr;
        PDFResourceOwnerKind kind = PDFResourceOwnerKind::Form;
        const PDFObject* resources = nullptr;
    };

    /// The categories of a resource dictionary, which hold owners of resources
    enum class Category
    {
        XObject,
        Pattern,
        Font,
        ExtGState
    };

    /// No more edges may be added (the budget of the edges is spent)
    bool isEdgeBudgetSpent() const
    {
        return isEdgeLimitReached;
    }

    /// Enters an owner reached from the node \p parent (SIZE_MAX = none, a page): a new node is
    /// queued, a node entered before gets only the edge
    void addOwner(size_t parent, const PDFObject& ownerObject, PDFResourceOwnerKind kind, const PDFObject& resources, PDFObjectReference annotation)
    {
        if (parent != SIZE_MAX && edgeCount >= maximumEdges)
        {
            isEdgeLimitReached = true;
            return;
        }

        const int depth = parent == SIZE_MAX ? 0 : result.nodes[parent].depth + 1;
        PDFObjectReference reference;
        if (ownerObject.isReference())
        {
            reference = ownerObject.getReference();
            auto it = nodeOfReference.find(reference);
            if (it != nodeOfReference.cend())
            {
                addEdge(parent, it->second);
                return;
            }
        }

        // A direct owner is the object at its address in the storage (a page has none)
        const PDFObject* directObject = (parent != SIZE_MAX && !ownerObject.isReference()) ? &ownerObject : nullptr;
        if (directObject)
        {
            auto it = nodeOfDirectObject.find(directObject);
            if (it != nodeOfDirectObject.cend())
            {
                addEdge(parent, it->second);
                return;
            }
        }

        if (!walk.enter(ownerObject, depth))
        {
            return;
        }

        PDFResourceWalkNode node;
        node.kind = kind;
        node.reference = reference;
        node.object = document->getObject(ownerObject);
        node.resources = document->getObject(resources);
        node.pageIndex = parent == SIZE_MAX ? currentPage : result.nodes[parent].pageIndex;
        node.annotation = annotation;
        node.depth = depth;

        const size_t index = result.nodes.size();
        result.nodes.push_back(std::move(node));
        if (reference.isValid())
        {
            nodeOfReference[reference] = index;
        }
        if (directObject)
        {
            nodeOfDirectObject[directObject] = index;
        }
        addEdge(parent, index);
        queue.push_back(index);
    }

    void addEdge(size_t parent, size_t child)
    {
        if (parent != SIZE_MAX)
        {
            result.nodes[parent].children.push_back(child);
            ++edgeCount;
        }
    }

    /// The owners of the resources of the node (forms, patterns, Type 3 fonts, soft mask groups)
    void readResources(size_t index)
    {
        // A copy: the vector of the nodes grows while the children are added
        const PDFObject resources = result.nodes[index].resources;
        const PDFDictionary* resourcesDictionary = document->getDictionaryFromObject(resources);
        if (!resourcesDictionary)
        {
            return;
        }

        const std::pair<Category, const char*> categories[] = { { Category::XObject, "XObject" }, { Category::Pattern, "Pattern" },
                                                                { Category::Font, "Font" }, { Category::ExtGState, "ExtGState" } };
        for (const auto& category : categories)
        {
            const PDFDictionary* dictionary = document->getDictionaryFromObject(resourcesDictionary->get(category.second));
            if (!dictionary)
            {
                continue;
            }

            // The map keeps its elements in place, while the nodes are added
            for (const Candidate& candidate : getCandidates(dictionary, category.first))
            {
                if (isEdgeBudgetSpent())
                {
                    return;
                }
                addOwner(index, *candidate.owner, candidate.kind, *candidate.resources, PDFObjectReference());
            }
        }
    }

    /// The owners of resources of a category dictionary, found once for the whole walk
    const std::vector<Candidate>& getCandidates(const PDFDictionary* dictionary, Category category)
    {
        auto it = candidatesOfDictionary.find(std::make_pair(dictionary, category));
        if (it != candidatesOfDictionary.cend())
        {
            return it->second;
        }

        std::vector<Candidate> candidates;
        auto addCandidate = [&candidates](const PDFObject& owner, PDFResourceOwnerKind kind, const PDFObject& resources)
        {
            Candidate candidate;
            candidate.owner = &owner;
            candidate.kind = kind;
            candidate.resources = &resources;
            candidates.push_back(candidate);
        };

        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            const PDFObject& value = dictionary->getValue(i);
            switch (category)
            {
                case Category::XObject:
                {
                    // Forms of /XObject (8.10, Table 93: /Subtype /Form)
                    const PDFObject& object = document->getObject(value);
                    if (object.isStream() && isName(object.getStream()->getDictionary()->get("Subtype"), "Form"))
                    {
                        addCandidate(value, PDFResourceOwnerKind::Form, object.getStream()->getDictionary()->get("Resources"));
                    }
                    break;
                }

                case Category::Pattern:
                {
                    // Tiling patterns of /Pattern (8.7.3.1, Table 75: /PatternType 1 has /Resources)
                    const PDFDictionary* pattern = document->getDictionaryFromObject(value);
                    const PDFObject patternType = pattern ? document->getObject(pattern->get("PatternType")) : PDFObject();
                    if (pattern && patternType.isInt() && patternType.getInteger() == 1)
                    {
                        addCandidate(value, PDFResourceOwnerKind::Pattern, pattern->get("Resources"));
                    }
                    break;
                }

                case Category::Font:
                {
                    // Type 3 fonts of /Font (9.6.4, Table 110: /Resources of the glyph descriptions)
                    const PDFDictionary* font = document->getDictionaryFromObject(value);
                    if (font && isName(font->get("Subtype"), "Type3"))
                    {
                        addCandidate(value, PDFResourceOwnerKind::Type3Font, font->get("Resources"));
                    }
                    break;
                }

                case Category::ExtGState:
                {
                    // Soft masks of /ExtGState (11.6.5.2, Table 142: /G is a transparency group XObject)
                    const PDFDictionary* state = document->getDictionaryFromObject(value);
                    const PDFDictionary* softMask = state ? document->getDictionaryFromObject(state->get("SMask")) : nullptr;
                    if (softMask)
                    {
                        const PDFObject& group = softMask->get("G");
                        const PDFObject& groupObject = document->getObject(group);
                        if (groupObject.isStream())
                        {
                            addCandidate(group, PDFResourceOwnerKind::SoftMaskGroup, groupObject.getStream()->getDictionary()->get("Resources"));
                        }
                    }
                    break;
                }
            }
        }

        return candidatesOfDictionary.emplace(std::make_pair(dictionary, category), std::move(candidates)).first->second;
    }

    /// The appearance streams of the annotations of a page (12.5.5, Table 170: /N, /R, /D are a stream
    /// or a dictionary of the streams of the states)
    void readAnnotations(size_t pageNode, const PDFPage* page)
    {
        for (const PDFObjectReference annotationReference : page->getAnnotations())
        {
            if (isEdgeBudgetSpent())
            {
                return;
            }

            const PDFDictionary* annotation = document->getDictionaryFromObject(document->getObjectByReference(annotationReference));
            const PDFDictionary* appearance = annotation ? document->getDictionaryFromObject(annotation->get("AP")) : nullptr;
            if (!appearance)
            {
                continue;
            }

            for (const char* key : { "N", "R", "D" })
            {
                const PDFObject& entry = appearance->get(key);
                const PDFObject& entryObject = document->getObject(entry);
                if (entryObject.isStream())
                {
                    addOwner(pageNode, entry, PDFResourceOwnerKind::AppearanceStream, entryObject.getStream()->getDictionary()->get("Resources"), annotationReference);
                }
                else if (entryObject.isDictionary())
                {
                    const PDFDictionary* states = entryObject.getDictionary();
                    for (size_t i = 0; i < states->getCount(); ++i)
                    {
                        const PDFObject& state = states->getValue(i);
                        const PDFObject& stateObject = document->getObject(state);
                        if (stateObject.isStream())
                        {
                            addOwner(pageNode, state, PDFResourceOwnerKind::AppearanceStream, stateObject.getStream()->getDictionary()->get("Resources"), annotationReference);
                        }
                    }
                }
            }
        }
    }

    bool isName(const PDFObject& object, const char* name) const
    {
        const PDFObject& dereferenced = document->getObject(object);
        return dereferenced.isName() && dereferenced.getString() == name;
    }

    const PDFDocument* document;
    PDFObjectWalk walk;
    PDFResourceWalkResult result;
    std::map<PDFObjectReference, size_t> nodeOfReference;
    std::map<const PDFObject*, size_t> nodeOfDirectObject;                              ///< The direct owners by their address in the storage
    std::map<std::pair<const PDFDictionary*, Category>, std::vector<Candidate>> candidatesOfDictionary;
    std::deque<size_t> queue;
    PDFInteger currentPage = -1;
    qint64 maximumEdges = 0;
    qint64 edgeCount = 0;
    bool isEdgeLimitReached = false;
};

PDFResourceWalkResult PDFResourceWalk::walk(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, const Options& options, const PDFOperationControl* control)
{
    if (!document)
    {
        return PDFResourceWalkResult();
    }

    PDFResourceWalkState state(document, options);
    const PDFCatalog* catalog = document->getCatalog();
    const PDFInteger pageCount = PDFInteger(catalog->getPageCount());
    std::set<PDFInteger> walkedPages;

    // The pages first (depth 0), so every owner is entered by its shortest path from a page; the page of
    // a node is the first page, which reached it
    for (const PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex < 0 || pageIndex >= pageCount || !walkedPages.insert(pageIndex).second)
        {
            continue;
        }

        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        const PDFObjectReference pageReference = page->getPageReference();
        const size_t nodeCount = state.result.nodes.size();
        state.currentPage = pageIndex;
        state.addOwner(SIZE_MAX, pageReference.isValid() ? PDFObject::createReference(pageReference) : PDFObject(), PDFResourceOwnerKind::Page, page->getResources(), PDFObjectReference());
        if (state.result.nodes.size() == nodeCount)
        {
            // A page refused by the budget, or one page object listed twice in the page tree: its node
            // is the one entered before, if any
            auto it = pageReference.isValid() ? state.nodeOfReference.find(pageReference) : state.nodeOfReference.end();
            if (it != state.nodeOfReference.end())
            {
                state.result.pages.emplace_back(pageIndex, it->second);
            }
            continue;
        }

        const size_t pageNode = state.result.nodes.size() - 1;
        state.result.pages.emplace_back(pageIndex, pageNode);
        if (options.includeAnnotations)
        {
            state.readAnnotations(pageNode, page);
        }
    }

    // Breadth first: the queue holds the pages and the appearance streams, then their owners
    while (!state.queue.empty())
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            state.result.isCanceled = true;
            break;
        }

        if (state.isEdgeBudgetSpent())
        {
            break;
        }

        const size_t index = state.queue.front();
        state.queue.pop_front();
        state.readResources(index);
    }

    state.result.isLimitReached = state.walk.isLimitReached() || state.isEdgeBudgetSpent();
    return std::move(state.result);
}

std::vector<PDFInteger> PDFResourceWalkResult::getPageIndices(size_t nodeIndex) const
{
    std::vector<PDFInteger> result;
    if (nodeIndex >= nodes.size())
    {
        return result;
    }

    // The parents of every node, then a walk backwards with one visited set
    std::vector<std::vector<size_t>> parents(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        for (const size_t child : nodes[i].children)
        {
            if (child < nodes.size())
            {
                parents[child].push_back(i);
            }
        }
    }

    std::vector<bool> visited(nodes.size(), false);
    std::deque<size_t> queue = { nodeIndex };
    visited[nodeIndex] = true;
    std::set<size_t> pageNodes;
    while (!queue.empty())
    {
        const size_t index = queue.front();
        queue.pop_front();
        if (nodes[index].kind == PDFResourceOwnerKind::Page)
        {
            pageNodes.insert(index);
        }

        for (const size_t parent : parents[index])
        {
            if (!visited[parent])
            {
                visited[parent] = true;
                queue.push_back(parent);
            }
        }
    }

    for (const auto& [pageIndex, pageNode] : pages)
    {
        if (pageNodes.count(pageNode))
        {
            result.push_back(pageIndex);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

}   // namespace pdf
