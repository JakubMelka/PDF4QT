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

// Edit Content, stage E0, package P04: the writer of a number tree (ISO 32000-2, 7.9.7, Table 37).
//
// The shape of the written tree: the keys ascending; the leaves are filled one after the other
// with KEYS_PER_LEAF keys, the last one has the rest; the nodes of a level are grouped the same
// way by KIDS_PER_NODE, until at most KIDS_PER_NODE of them are left - they are the kids of the
// root. So every leaf is at the same depth. A tree of at most KEYS_PER_LEAF keys is the root alone
// with /Nums. /Limits is in every node except the root.

#include "pdfnumbertreewriter.h"

#include "pdfdocumentbuilder.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

namespace pdf
{

/// Nodes of the written number tree. Threads and ownership: static functions, values only.
class PDFNumberTreeWriterHelper
{
public:
    PDFNumberTreeWriterHelper() = delete;

    using Items = std::map<PDFInteger, PDFObject>;

    /// A written node, which is not the root
    struct Node
    {
        PDFObjectReference reference;
        PDFInteger least = 0;       ///< The least key of the leaves under the node
        PDFInteger greatest = 0;    ///< The greatest one
    };

    /// The array /Nums [key1 value1 key2 value2 ...] of the items \p begin ... \p end
    static PDFObject createNumbers(Items::const_iterator begin, Items::const_iterator end, size_t count)
    {
        PDFArrayBuilder numbers;
        numbers.reserve(2 * count);
        for (auto it = begin; it != end; ++it)
        {
            numbers.appendItem(PDFObject::createInteger(it->first));
            numbers.appendItem(it->second);
        }

        return PDFObject::createArray(std::move(numbers));
    }

    static PDFObject createLimits(PDFInteger least, PDFInteger greatest)
    {
        PDFArrayBuilder limits;
        limits.reserve(2);
        limits.appendItem(PDFObject::createInteger(least));
        limits.appendItem(PDFObject::createInteger(greatest));
        return PDFObject::createArray(std::move(limits));
    }

    /// The array /Kids of the nodes \p first ... \p first + \p count - 1
    static PDFObject createKids(const std::vector<Node>& nodes, size_t first, size_t count)
    {
        PDFArrayBuilder kids;
        kids.reserve(count);
        for (size_t i = first; i < first + count; ++i)
        {
            kids.appendItem(PDFObject::createReference(nodes[i].reference));
        }

        return PDFObject::createArray(std::move(kids));
    }

    /// Adds a node, which is not the root, to the document
    static Node addNode(PDFDocumentBuilder* builder, const char* contentKey, PDFObject content, PDFInteger least, PDFInteger greatest)
    {
        PDFDictionaryBuilder dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString(contentKey), std::move(content));
        dictionary.addEntry(PDFInplaceOrMemoryString("Limits"), createLimits(least, greatest));

        // The object is set, not added with its value: PDFDocumentBuilder::addObject removes the
        // null entries of a dictionary, and a value of the tree is written as it is
        Node node;
        node.reference = builder->addObject(PDFObject());
        builder->setObject(node.reference, PDFObject::createDictionary(std::move(dictionary)));
        node.least = least;
        node.greatest = greatest;
        return node;
    }
};

PDFObject PDFNumberTreeWriter::write(PDFDocumentBuilder* builder, const std::map<PDFInteger, PDFObject>& items)
{
    PDFDictionaryBuilder root;

    // A small tree is its root. Without a builder there is nowhere to put a node: the root
    // holds all keys, which is a valid tree too (Table 37: /Nums of the root has no limit).
    if (items.size() <= KEYS_PER_LEAF || !builder)
    {
        root.addEntry(PDFInplaceOrMemoryString("Nums"), PDFNumberTreeWriterHelper::createNumbers(items.cbegin(), items.cend(), items.size()));
        return PDFObject::createDictionary(std::move(root));
    }

    // The leaves
    std::vector<PDFNumberTreeWriterHelper::Node> nodes;
    nodes.reserve(items.size() / KEYS_PER_LEAF + 1);

    size_t remaining = items.size();
    auto it = items.cbegin();
    while (it != items.cend())
    {
        const size_t count = std::min(remaining, KEYS_PER_LEAF);
        auto itEnd = std::next(it, static_cast<std::ptrdiff_t>(count));
        nodes.push_back(PDFNumberTreeWriterHelper::addNode(builder, "Nums", PDFNumberTreeWriterHelper::createNumbers(it, itEnd, count), it->first, std::prev(itEnd)->first));
        remaining -= count;
        it = itEnd;
    }

    // The intermediate levels, until the nodes fit into the root
    while (nodes.size() > KIDS_PER_NODE)
    {
        std::vector<PDFNumberTreeWriterHelper::Node> parents;
        parents.reserve(nodes.size() / KIDS_PER_NODE + 1);

        for (size_t first = 0; first < nodes.size(); first += KIDS_PER_NODE)
        {
            const size_t count = std::min(nodes.size() - first, KIDS_PER_NODE);
            parents.push_back(PDFNumberTreeWriterHelper::addNode(builder, "Kids", PDFNumberTreeWriterHelper::createKids(nodes, first, count), nodes[first].least, nodes[first + count - 1].greatest));
        }

        nodes = std::move(parents);
    }

    root.addEntry(PDFInplaceOrMemoryString("Kids"), PDFNumberTreeWriterHelper::createKids(nodes, 0, nodes.size()));
    return PDFObject::createDictionary(std::move(root));
}

}   // namespace pdf
