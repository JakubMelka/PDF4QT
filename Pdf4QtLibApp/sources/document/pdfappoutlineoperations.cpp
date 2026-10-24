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

#include "pdfappannotationoperations.h"

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfencoding.h"
#include "pdfexception.h"

#include <cmath>
#include <memory>
#include <set>

namespace pdf
{

/// Outline of a document as a tree of the dictionaries of its items (the
/// operations of package C edit this tree and write it back)
class PDFAppOutlineTree
{
public:
    /// Item of the outline
    class Node
    {
    public:
        PDFObjectReference reference;   ///< Object of the item (invalid = a new item)
        PDFObject dictionary;           ///< Entries of the item (a dictionary, also of a new item)
        bool isOpen = true;             ///< The item shows its children (/Count is not negative)
        PDFInteger visibleCount = 0;    ///< Number of the visible descendants (computed by the writer)
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    PDFAppOutlineTree() = default;
    PDFAppOutlineTree(const PDFAppOutlineTree&) = delete;
    PDFAppOutlineTree& operator=(const PDFAppOutlineTree&) = delete;

    PDFObjectReference rootReference;                   ///< Object of the outline dictionary (invalid = none)
    PDFObject rootDictionary;                           ///< Entries of the outline dictionary (null = none)
    Node root;                                          ///< Invisible root (its children are the top level items)
    std::vector<PDFObjectReference> originalReferences; ///< Items read from the document
    size_t count = 0;                                   ///< Number of the items read
};

/// Reading, checking and writing of the outline tree
class PDFAppOutlineOperationsHelper
{
public:
    PDFAppOutlineOperationsHelper() = delete;

    using Node = PDFAppOutlineTree::Node;
    using OutlinePath = PDFAppAnnotationOperations::OutlinePath;

    /// Catalog dictionary of the document (nullptr if none)
    static const PDFDictionary* getCatalogDictionary(const PDFDocument* document)
    {
        const PDFObjectStorage& storage = document->getStorage();
        const PDFDictionary* trailer = storage.getDictionaryFromObject(storage.getTrailerDictionary());
        return trailer ? storage.getDictionaryFromObject(trailer->get("Root")) : nullptr;
    }

    /// Title of an item dictionary (as PDFOutlineItem::parse reads it)
    static QString getTitle(const PDFObjectStorage& storage, const PDFObject& dictionary)
    {
        const PDFDictionary* entries = storage.getDictionaryFromObject(dictionary);
        const PDFObject& title = entries ? storage.getObject(entries->get("Title")) : PDFObject();
        return title.isString() ? PDFEncoding::convertTextString(title.getString()) : QString();
    }

    /// The tree read from the document is the outline, which the catalog has
    /// parsed (the same items with the same titles): only then the paths of the
    /// user interface address the items of the tree
    static bool isSameOutline(const PDFObjectStorage& storage, const Node& root, const PDFOutlineItem* parsedRoot)
    {
        if (!parsedRoot)
        {
            return root.children.empty();
        }

        std::vector<std::pair<const Node*, const PDFOutlineItem*>> stack = { { &root, parsedRoot } };
        while (!stack.empty())
        {
            const auto [node, item] = stack.back();
            stack.pop_back();
            if (node->children.size() != item->getChildCount())
            {
                return false;
            }

            for (size_t i = 0; i < node->children.size(); ++i)
            {
                const Node* child = node->children[i].get();
                const PDFOutlineItem* childItem = item->getChild(i);
                if (!childItem || getTitle(storage, child->dictionary) != childItem->getTitle())
                {
                    return false;
                }
                stack.emplace_back(child, childItem);
            }
        }
        return true;
    }

    /// The dictionary of the reference can be an outline item: it is not the
    /// catalog or the document information of the trailer and it is not an object
    /// of another structure - a page, a page tree node, an annotation, an XObject
    /// (/Type other than /Outlines, /Subtype, /Kids). The editing writes the
    /// structure entries of every item and empties a removed item.
    static bool isOutlineItem(const PDFObjectStorage& storage, PDFObjectReference reference, const PDFDictionary* dictionary)
    {
        if (const PDFDictionary* trailer = storage.getDictionaryFromObject(storage.getTrailerDictionary()))
        {
            for (const char* key : { "Root", "Info" })
            {
                const PDFObject& object = trailer->get(key);
                if (object.isReference() && object.getReference() == reference)
                {
                    return false;
                }
            }
        }

        if (dictionary->hasKey("Subtype") || dictionary->hasKey("Kids"))
        {
            return false;
        }

        const PDFObject& type = storage.getObject(dictionary->get("Type"));
        return type.isNull() || (type.isName() && type.getString() == "Outlines");
    }

    /// Reads the outline of the document; nullptr, when it cannot be edited
    /// (damaged, too large, too deep or not the outline of the catalog)
    static std::unique_ptr<PDFAppOutlineTree> read(const PDFDocument* document)
    {
        if (!document)
        {
            return nullptr;
        }

        const PDFObjectStorage& storage = document->getStorage();
        const PDFDictionary* catalog = getCatalogDictionary(document);
        if (!catalog)
        {
            return nullptr;
        }

        // The tree is not moved (its nodes point to their parents, also to the root)
        std::unique_ptr<PDFAppOutlineTree> result = std::make_unique<PDFAppOutlineTree>();
        PDFAppOutlineTree& tree = *result;
        const PDFObject& outlines = catalog->get("Outlines");
        const PDFObject& rootObject = storage.getObject(outlines);
        if (rootObject.isDictionary())
        {
            tree.rootDictionary = rootObject;
            tree.rootReference = outlines.isReference() ? outlines.getReference() : PDFObjectReference();
        }

        // The items are walked without recursion (an outline may be deep); every
        // item is taken once (an item reached again ends its chain, as the parser)
        struct Frame
        {
            Node* parent = nullptr;
            PDFObject next;
            size_t depth = 0;
        };

        std::vector<Frame> stack;
        std::set<PDFObjectReference> visited;
        if (tree.rootDictionary.isDictionary())
        {
            const PDFObject& first = tree.rootDictionary.getDictionary()->get("First");
            if (first.isReference())
            {
                stack.push_back(Frame{ &tree.root, first, 1 });
            }
        }

        PDFDocumentDataLoaderDecorator loader(&storage);
        while (!stack.empty())
        {
            Frame& frame = stack.back();
            if (!frame.next.isReference())
            {
                stack.pop_back();
                continue;
            }

            const PDFObjectReference reference = frame.next.getReference();
            frame.next = PDFObject();
            if (!visited.insert(reference).second)
            {
                stack.pop_back();
                continue;
            }

            const PDFObject& object = storage.getObjectByReference(reference);
            if (!object.isDictionary())
            {
                stack.pop_back();
                continue;
            }

            // An object of another structure reached by /First or /Next (the
            // parser shows it as an item) would be rewritten or emptied by the
            // editing: such an outline is read only
            if (!isOutlineItem(storage, reference, object.getDictionary()))
            {
                return nullptr;
            }

            if (++tree.count > PDFAppAnnotationOperations::MAXIMUM_OUTLINE_ITEMS)
            {
                return nullptr;
            }

            const PDFDictionary* dictionary = object.getDictionary();
            std::unique_ptr<Node> node = std::make_unique<Node>();
            node->reference = reference;
            node->dictionary = object;
            node->isOpen = loader.readIntegerFromDictionary(dictionary, "Count", 0) >= 0;
            node->parent = frame.parent;
            Node* nodePointer = node.get();
            frame.parent->children.push_back(std::move(node));
            tree.originalReferences.push_back(reference);

            frame.next = dictionary->get("Next");
            const size_t depth = frame.depth;
            const PDFObject& first = dictionary->get("First");
            if (first.isReference())
            {
                if (depth >= PDFAppAnnotationOperations::MAXIMUM_OUTLINE_DEPTH)
                {
                    return nullptr;
                }

                // The frame reference is not used after the push (reallocation)
                stack.push_back(Frame{ nodePointer, first, depth + 1 });
            }
        }

        const QSharedPointer<PDFOutlineItem> parsedRoot = document->getCatalog()->getOutlineRootPtr();
        if (!isSameOutline(storage, tree.root, parsedRoot.data()))
        {
            return nullptr;
        }
        return result;
    }

    /// Node of the path (the root for an empty path), nullptr for an invalid path
    static Node* find(PDFAppOutlineTree& tree, const OutlinePath& path)
    {
        Node* node = &tree.root;
        for (size_t index : path)
        {
            if (index >= node->children.size())
            {
                return nullptr;
            }
            node = node->children[index].get();
        }
        return node;
    }

    /// Index of the node among the children of its parent
    static size_t getIndex(const Node* node)
    {
        const Node* parent = node->parent;
        for (size_t i = 0; parent && i < parent->children.size(); ++i)
        {
            if (parent->children[i].get() == node)
            {
                return i;
            }
        }
        return 0;
    }

    /// Path of the node
    static OutlinePath getPath(const PDFAppOutlineTree& tree, const Node* node)
    {
        OutlinePath path;
        for (const Node* current = node; current && current != &tree.root; current = current->parent)
        {
            path.insert(path.begin(), getIndex(current));
        }
        return path;
    }

    /// Nodes of the tree in the pre-order (without the root)
    static std::vector<Node*> getNodes(PDFAppOutlineTree& tree)
    {
        std::vector<Node*> nodes;
        nodes.reserve(tree.count);
        std::vector<Node*> stack;
        for (size_t i = tree.root.children.size(); i > 0; --i)
        {
            stack.push_back(tree.root.children[i - 1].get());
        }
        while (!stack.empty())
        {
            Node* node = stack.back();
            stack.pop_back();
            nodes.push_back(node);
            for (size_t i = node->children.size(); i > 0; --i)
            {
                stack.push_back(node->children[i - 1].get());
            }
        }
        return nodes;
    }

    /// Text string object of the title
    static PDFObject createTextString(const QString& text)
    {
        PDFObjectFactory factory;
        factory << text;
        return factory.takeObject();
    }

    /// Object of the destination (an array, or a name of a named destination)
    static PDFObject createDestination(const PDFDestination& destination)
    {
        PDFObjectFactory factory;
        factory << destination;
        return factory.takeObject();
    }

    /// Sets the entry of the dictionary object
    static void setEntry(PDFObject* dictionary, const char* key, PDFObject value)
    {
        PDFDictionaryBuilder entries;
        if (dictionary->isDictionary())
        {
            entries = PDFDictionaryBuilder(*dictionary->getDictionary());
        }
        if (value.isNull())
        {
            entries.removeEntry(key);
        }
        else
        {
            entries.setEntry(PDFInplaceOrMemoryString(key), std::move(value));
        }
        *dictionary = PDFObject::createDictionary(std::move(entries));
    }

    /// A destination of the document, which an item can have: a valid page of the
    /// document (the reference is completed from the index) or a named
    /// destination; the numbers are finite (or undefined - NaN). nullopt otherwise.
    static std::optional<PDFDestination> checkDestination(const PDFDocument* document, PDFDestination destination)
    {
        if (!destination.isValid())
        {
            return std::nullopt;
        }

        if (destination.isNamedDestination())
        {
            const QByteArray& name = destination.getName();
            if (name.isEmpty() || name.size() > PDFAppAnnotationOperations::MAXIMUM_OUTLINE_TITLE_LENGTH)
            {
                return std::nullopt;
            }
            return destination;
        }

        const PDFCatalog* catalog = document->getCatalog();
        size_t pageIndex = destination.getPageReference().isValid() ? catalog->getPageIndexFromPageReference(destination.getPageReference()) : PDFCatalog::INVALID_PAGE_INDEX;
        if (pageIndex == PDFCatalog::INVALID_PAGE_INDEX && !destination.getPageReference().isValid() &&
            destination.getPageIndex() >= 0 && destination.getPageIndex() < PDFInteger(catalog->getPageCount()))
        {
            pageIndex = size_t(destination.getPageIndex());
        }
        if (pageIndex == PDFCatalog::INVALID_PAGE_INDEX || pageIndex >= catalog->getPageCount())
        {
            return std::nullopt;
        }

        // Numbers: NaN = undefined (null), infinity is refused, huge values are bounded
        constexpr PDFReal limit = 1.0e7;
        for (PDFReal value : { destination.getLeft(), destination.getTop(), destination.getRight(), destination.getBottom(), destination.getZoom() })
        {
            if (std::isinf(value))
            {
                return std::nullopt;
            }
        }
        destination.setLeft(boundNumber(destination.getLeft(), limit));
        destination.setTop(boundNumber(destination.getTop(), limit));
        destination.setRight(boundNumber(destination.getRight(), limit));
        destination.setBottom(boundNumber(destination.getBottom(), limit));
        destination.setZoom(std::isnan(destination.getZoom()) ? destination.getZoom() : qBound(0.0, destination.getZoom(), 640.0));
        destination.setPageReference(catalog->getPage(pageIndex)->getPageReference());
        destination.setPageIndex(PDFInteger(pageIndex));
        return destination;
    }

    /// The number bounded to -limit .. limit (NaN stays NaN - an undefined value)
    static PDFReal boundNumber(PDFReal value, PDFReal limit)
    {
        return std::isnan(value) ? value : qBound(-limit, value, limit);
    }

    /// Number of the visible descendants of the node (its children are computed)
    static void computeVisibleCount(Node* node)
    {
        PDFInteger visible = 0;
        for (const std::unique_ptr<Node>& child : node->children)
        {
            visible += 1 + (child->isOpen ? child->visibleCount : 0);
        }
        node->visibleCount = visible;
    }

    /// Writes /First and /Last of the node (its /Count is written by the caller)
    static void writeStructure(PDFDictionaryBuilder& entries, const Node* node)
    {
        entries.removeEntry("First");
        entries.removeEntry("Last");
        entries.removeEntry("Count");
        if (!node->children.empty())
        {
            entries.setEntry(PDFInplaceOrMemoryString("First"), PDFObject::createReference(node->children.front()->reference));
            entries.setEntry(PDFInplaceOrMemoryString("Last"), PDFObject::createReference(node->children.back()->reference));
        }
    }

    /// Dictionary of a new item
    static PDFObject createItem(const QString& title, const PDFDestination& destination)
    {
        PDFObject dictionary;
        setEntry(&dictionary, "Title", createTextString(title));
        setEntry(&dictionary, "Dest", createDestination(destination));
        return dictionary;
    }

    /// Writes the tree into a modified document (flag Outline); nullopt when
    /// nothing has changed or the document cannot be built
    static PDFAppAnnotationResult write(const PDFDocument* document, PDFAppOutlineTree& tree)
    {
        try
        {
            PDFDocumentModifier modifier(document);
            PDFDocumentBuilder* builder = modifier.getBuilder();

            // References: an item keeps its object, a new item gets a new one
            std::vector<Node*> nodes = getNodes(tree);
            std::set<PDFObjectReference> used;
            for (Node* node : nodes)
            {
                if (!node->reference.isValid() || !used.insert(node->reference).second)
                {
                    node->reference = builder->addObject(PDFObject());
                    used.insert(node->reference);
                }
            }

            PDFObjectReference rootReference = tree.rootReference;
            if (!rootReference.isValid() || used.count(rootReference))
            {
                rootReference = builder->addObject(PDFObject());
            }

            // Visible descendants (/Count) from the leaves
            for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
            {
                computeVisibleCount(*it);
            }
            computeVisibleCount(&tree.root);

            // The structure of every item is written again
            std::vector<Node*> parents = nodes;
            parents.insert(parents.begin(), &tree.root);
            for (Node* parent : parents)
            {
                const PDFObjectReference parentReference = parent == &tree.root ? rootReference : parent->reference;
                for (size_t i = 0; i < parent->children.size(); ++i)
                {
                    Node* node = parent->children[i].get();
                    PDFDictionaryBuilder entries;
                    if (node->dictionary.isDictionary())
                    {
                        entries = PDFDictionaryBuilder(*node->dictionary.getDictionary());
                    }
                    entries.removeEntry("Parent");
                    entries.removeEntry("Prev");
                    entries.removeEntry("Next");
                    entries.setEntry(PDFInplaceOrMemoryString("Parent"), PDFObject::createReference(parentReference));
                    if (i > 0)
                    {
                        entries.setEntry(PDFInplaceOrMemoryString("Prev"), PDFObject::createReference(parent->children[i - 1]->reference));
                    }
                    if (i + 1 < parent->children.size())
                    {
                        entries.setEntry(PDFInplaceOrMemoryString("Next"), PDFObject::createReference(parent->children[i + 1]->reference));
                    }
                    writeStructure(entries, node);
                    if (!node->children.empty())
                    {
                        entries.setEntry(PDFInplaceOrMemoryString("Count"), PDFObject::createInteger(node->isOpen ? node->visibleCount : -node->visibleCount));
                    }
                    builder->setObject(node->reference, PDFObject::createDictionary(std::move(entries)));
                }
            }

            // The outline dictionary keeps its other entries
            PDFDictionaryBuilder rootEntries;
            if (tree.rootDictionary.isDictionary())
            {
                rootEntries = PDFDictionaryBuilder(*tree.rootDictionary.getDictionary());
            }
            writeStructure(rootEntries, &tree.root);
            if (!rootEntries.hasKey("Type"))
            {
                rootEntries.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName(QByteArray("Outlines")));
            }
            if (tree.root.visibleCount > 0)
            {
                rootEntries.setEntry(PDFInplaceOrMemoryString("Count"), PDFObject::createInteger(tree.root.visibleCount));
            }
            builder->setObject(rootReference, PDFObject::createDictionary(std::move(rootEntries)));

            const PDFDictionary* catalog = getCatalogDictionary(document);
            const PDFObject& outlines = catalog ? catalog->get("Outlines") : PDFObject();
            if (!outlines.isReference() || outlines.getReference() != rootReference)
            {
                builder->setOutline(rootReference);
            }

            // A removed item is not referenced any more: its object is emptied
            for (const PDFObjectReference& reference : tree.originalReferences)
            {
                if (!used.count(reference))
                {
                    builder->setObject(reference, PDFObject());
                }
            }

            if (!modifier.finalize() || !modifier.getDocument())
            {
                return std::nullopt;
            }
            return PDFModifiedDocument(modifier.getDocument(), nullptr, PDFModifiedDocument::Outline);
        }
        catch (const PDFException&)
        {
            return std::nullopt;
        }
    }

    /// The node \p ancestor is \p node or one of its parents
    static bool isAncestor(const Node* ancestor, const Node* node)
    {
        for (const Node* current = node; current; current = current->parent)
        {
            if (current == ancestor)
            {
                return true;
            }
        }
        return false;
    }
};

QString PDFAppAnnotationOperations::sanitizeOutlineTitle(const QString& title)
{
    QString result = title.left(MAXIMUM_OUTLINE_TITLE_LENGTH);
    for (QChar& character : result)
    {
        const QChar::Category category = character.category();
        if (category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Other_Surrogate ||
            character == QChar::LineSeparator || character == QChar::ParagraphSeparator)
        {
            character = QChar(u' ');
        }
    }
    return result.simplified();
}

bool PDFAppAnnotationOperations::isOutlineEditable(const PDFDocument* document)
{
    return PDFAppOutlineOperationsHelper::read(document) != nullptr;
}

std::vector<PDFObjectReference> PDFAppAnnotationOperations::getOutlineItemReferences(const PDFDocument* document)
{
    std::unique_ptr<PDFAppOutlineTree> tree = PDFAppOutlineOperationsHelper::read(document);
    if (!tree)
    {
        return {};
    }

    std::vector<PDFObjectReference> references;
    for (const PDFAppOutlineTree::Node* node : PDFAppOutlineOperationsHelper::getNodes(*tree))
    {
        references.push_back(node->reference);
    }
    return references;
}

PDFAppAnnotationResult PDFAppAnnotationOperations::setOutline(const PDFDocumentPointer& document, const QSharedPointer<PDFOutlineItem>& root)
{
    using Helper = PDFAppOutlineOperationsHelper;
    using Node = PDFAppOutlineTree::Node;

    if (!document)
    {
        return std::nullopt;
    }

    // The old items are emptied (when the old outline is readable); a damaged
    // outline is replaced by a new outline dictionary
    std::unique_ptr<PDFAppOutlineTree> tree = Helper::read(document.data());
    if (!tree)
    {
        tree = std::make_unique<PDFAppOutlineTree>();
        const PDFDictionary* catalog = Helper::getCatalogDictionary(document.data());
        const PDFObject& outlines = catalog ? catalog->get("Outlines") : PDFObject();
        const PDFObject& rootObject = document->getStorage().getObject(outlines);
        if (rootObject.isDictionary())
        {
            PDFDictionaryBuilder entries(*rootObject.getDictionary());
            entries.removeEntry("First");
            entries.removeEntry("Last");
            entries.removeEntry("Count");
            tree->rootDictionary = PDFObject::createDictionary(std::move(entries));
        }
    }
    tree->root.children.clear();

    // The new items without recursion
    struct Entry
    {
        const PDFOutlineItem* item = nullptr;
        Node* parent = nullptr;
        size_t depth = 0;
    };
    std::vector<Entry> stack;
    if (root)
    {
        for (size_t i = root->getChildCount(); i > 0; --i)
        {
            stack.push_back(Entry{ root->getChild(i - 1), &tree->root, 1 });
        }
    }

    size_t count = 0;
    while (!stack.empty())
    {
        const Entry entry = stack.back();
        stack.pop_back();
        if (!entry.item)
        {
            continue;
        }
        if (++count > MAXIMUM_OUTLINE_ITEMS || entry.depth > MAXIMUM_OUTLINE_DEPTH)
        {
            return std::nullopt;
        }

        std::unique_ptr<Node> node = std::make_unique<Node>();
        node->parent = entry.parent;
        Helper::setEntry(&node->dictionary, "Title", Helper::createTextString(sanitizeOutlineTitle(entry.item->getTitle())));
        if (const PDFActionGoTo* goTo = dynamic_cast<const PDFActionGoTo*>(entry.item->getAction()))
        {
            if (std::optional<PDFDestination> destination = Helper::checkDestination(document.data(), goTo->getDestination()))
            {
                Helper::setEntry(&node->dictionary, "Dest", Helper::createDestination(*destination));
            }
        }
        const PDFInteger flags = (entry.item->isFontItalics() ? 1 : 0) | (entry.item->isFontBold() ? 2 : 0);
        if (flags)
        {
            Helper::setEntry(&node->dictionary, "F", PDFObject::createInteger(flags));
        }
        const QColor color = entry.item->getTextColor();
        if (color.isValid() && color != QColor(Qt::black))
        {
            PDFObjectFactory factory;
            factory << WrapAnnotationColor(color.toRgb());
            Helper::setEntry(&node->dictionary, "C", factory.takeObject());
        }

        Node* nodePointer = node.get();
        entry.parent->children.push_back(std::move(node));
        for (size_t i = entry.item->getChildCount(); i > 0; --i)
        {
            stack.push_back(Entry{ entry.item->getChild(i - 1), nodePointer, entry.depth + 1 });
        }
    }

    return Helper::write(document.data(), *tree);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::insertOutlineItem(const PDFDocumentPointer& document, const OutlinePath& parent, size_t index, const QString& title, const PDFDestination& destination)
{
    using Helper = PDFAppOutlineOperationsHelper;

    const QString text = sanitizeOutlineTitle(title);
    if (!document || text.isEmpty())
    {
        return std::nullopt;
    }

    const std::optional<PDFDestination> checkedDestination = Helper::checkDestination(document.data(), destination);
    std::unique_ptr<PDFAppOutlineTree> tree = Helper::read(document.data());
    PDFAppOutlineTree::Node* parentNode = tree ? Helper::find(*tree, parent) : nullptr;
    if (!checkedDestination || !parentNode || parent.size() >= MAXIMUM_OUTLINE_DEPTH || tree->count >= MAXIMUM_OUTLINE_ITEMS)
    {
        return std::nullopt;
    }

    std::unique_ptr<PDFAppOutlineTree::Node> node = std::make_unique<PDFAppOutlineTree::Node>();
    node->dictionary = Helper::createItem(text, *checkedDestination);
    node->parent = parentNode;
    const size_t position = qMin(index, parentNode->children.size());
    parentNode->children.insert(parentNode->children.begin() + std::ptrdiff_t(position), std::move(node));

    // The new item is visible in other readers too
    parentNode->isOpen = true;
    return Helper::write(document.data(), *tree);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::renameOutlineItem(const PDFDocumentPointer& document, const OutlinePath& item, const QString& title)
{
    using Helper = PDFAppOutlineOperationsHelper;

    const QString text = sanitizeOutlineTitle(title);
    if (!document || text.isEmpty() || item.empty())
    {
        return std::nullopt;
    }

    std::unique_ptr<PDFAppOutlineTree> tree = Helper::read(document.data());
    // The title is compared as the user interface shows it (sanitized): a title
    // with control characters, runs of spaces or a length over the limit, which
    // is confirmed unchanged, is not rewritten
    PDFAppOutlineTree::Node* node = tree ? Helper::find(*tree, item) : nullptr;
    if (!node || sanitizeOutlineTitle(Helper::getTitle(document->getStorage(), node->dictionary)) == text)
    {
        return std::nullopt;
    }

    Helper::setEntry(&node->dictionary, "Title", Helper::createTextString(text));
    return Helper::write(document.data(), *tree);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::removeOutlineItem(const PDFDocumentPointer& document, const OutlinePath& item)
{
    using Helper = PDFAppOutlineOperationsHelper;

    if (!document || item.empty())
    {
        return std::nullopt;
    }

    std::unique_ptr<PDFAppOutlineTree> tree = Helper::read(document.data());
    PDFAppOutlineTree::Node* node = tree ? Helper::find(*tree, item) : nullptr;
    if (!node)
    {
        return std::nullopt;
    }

    PDFAppOutlineTree::Node* parent = node->parent;
    parent->children.erase(parent->children.begin() + std::ptrdiff_t(item.back()));
    return Helper::write(document.data(), *tree);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::moveOutlineItem(const PDFDocumentPointer& document, const OutlinePath& item, const OutlinePath& newParent, size_t index, OutlinePath* newPath)
{
    using Helper = PDFAppOutlineOperationsHelper;
    using Node = PDFAppOutlineTree::Node;

    if (!document || item.empty())
    {
        return std::nullopt;
    }

    std::unique_ptr<PDFAppOutlineTree> tree = Helper::read(document.data());
    Node* node = tree ? Helper::find(*tree, item) : nullptr;
    Node* parent = tree ? Helper::find(*tree, newParent) : nullptr;
    if (!node || !parent || Helper::isAncestor(node, parent))
    {
        return std::nullopt;
    }

    // Depth of the moved subtree under its new parent
    size_t subtreeDepth = 1;
    {
        std::vector<std::pair<const Node*, size_t>> stack = { { node, 1 } };
        while (!stack.empty())
        {
            const auto [current, depth] = stack.back();
            stack.pop_back();
            subtreeDepth = qMax(subtreeDepth, depth);
            for (const std::unique_ptr<Node>& child : current->children)
            {
                stack.emplace_back(child.get(), depth + 1);
            }
        }
    }
    if (newParent.size() + subtreeDepth > MAXIMUM_OUTLINE_DEPTH)
    {
        return std::nullopt;
    }

    // The index is of the children before the move
    Node* oldParent = node->parent;
    const size_t oldIndex = item.back();
    size_t position = qMin(index, parent->children.size());
    if (parent == oldParent)
    {
        if (position == oldIndex || position == oldIndex + 1)
        {
            // The item stays at its place
            return std::nullopt;
        }
        if (position > oldIndex)
        {
            --position;
        }
    }

    std::unique_ptr<Node> moved = std::move(oldParent->children[oldIndex]);
    oldParent->children.erase(oldParent->children.begin() + std::ptrdiff_t(oldIndex));
    moved->parent = parent;
    parent->children.insert(parent->children.begin() + std::ptrdiff_t(position), std::move(moved));
    parent->isOpen = true;

    const Node* movedNode = parent->children[position].get();
    if (newPath)
    {
        *newPath = Helper::getPath(*tree, movedNode);
    }
    return Helper::write(document.data(), *tree);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::setOutlineItemDestination(const PDFDocumentPointer& document, const OutlinePath& item, const PDFDestination& destination)
{
    using Helper = PDFAppOutlineOperationsHelper;

    if (!document || item.empty())
    {
        return std::nullopt;
    }

    const std::optional<PDFDestination> checkedDestination = Helper::checkDestination(document.data(), destination);
    std::unique_ptr<PDFAppOutlineTree> tree = Helper::read(document.data());
    PDFAppOutlineTree::Node* node = tree ? Helper::find(*tree, item) : nullptr;
    if (!node || !checkedDestination)
    {
        return std::nullopt;
    }

    Helper::setEntry(&node->dictionary, "A", PDFObject());
    Helper::setEntry(&node->dictionary, "Dest", Helper::createDestination(*checkedDestination));
    return Helper::write(document.data(), *tree);
}

PDFAppAnnotationResult PDFAppAnnotationOperations::setOutlineItemStyle(const PDFDocumentPointer& document, const OutlinePath& item, bool bold, bool italic)
{
    using Helper = PDFAppOutlineOperationsHelper;

    if (!document || item.empty())
    {
        return std::nullopt;
    }

    std::unique_ptr<PDFAppOutlineTree> tree = Helper::read(document.data());
    PDFAppOutlineTree::Node* node = tree ? Helper::find(*tree, item) : nullptr;
    if (!node)
    {
        return std::nullopt;
    }

    // Bits 1 (italic) and 2 (bold); the other bits are kept
    PDFDocumentDataLoaderDecorator loader(&document->getStorage());
    const PDFInteger oldFlags = loader.readIntegerFromDictionary(node->dictionary.getDictionary(), "F", 0);
    const PDFInteger flags = (oldFlags & ~PDFInteger(3)) | (italic ? 1 : 0) | (bold ? 2 : 0);
    if (flags == oldFlags)
    {
        return std::nullopt;
    }

    Helper::setEntry(&node->dictionary, "F", flags ? PDFObject::createInteger(flags) : PDFObject());
    return Helper::write(document.data(), *tree);
}

}   // namespace pdf
