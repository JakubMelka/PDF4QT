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

// Edit Content, stage E0, package P04: the structure tree as the editor reads it and the
// consistency of the structure with the edits of the content (zadani_jadro.md 4.12).
//
// ISO 32000-2: 14.7.2 (Table 354 structure tree root, Table 355 structure element), 14.7.3 (role
// map), 14.7.5.2 (Table 357 marked-content reference), 14.7.5.3 (Table 358 object reference),
// 14.7.5.4 (parent tree, Table 359 /StructParent and /StructParents), 7.9.7 (Table 37 number tree).
//
// What the model does not understand, it keeps: the other keys of an element in otherEntries, the
// items of /K, which are no children, as foreign children at their places, the items of the parent
// tree, which name no element of the model, raw. Nothing of it is changed by an edit of the content,
// except a foreign child, which names exactly one marked content of a page: it goes (a direct
// element loses its /K) with the last content of that sequence.

#include "pdfcontentstructure.h"

#include "pdfdocument.h"
#include "pdfencoding.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfstructuretree.h"   // PDFStructureItem::getTypeFromName only: the names of the standard structure types

#include <QCryptographicHash>

#include <algorithm>
#include <atomic>
#include <deque>
#include <iterator>
#include <limits>
#include <tuple>

namespace pdf
{

/// Reading of the structure of a document and the changes of a model.
///
/// Threads and ownership: static functions; a Reader lives on the stack of one call of read and
/// points to the document only during that call. The functions, which change a model, get a copy
/// nobody else sees yet (PDFContentStructureModel::applied).
class PDFContentStructureModelHelper
{
public:
    PDFContentStructureModelHelper() = delete;

    using Model = PDFContentStructureModel;
    using Element = PDFContentStructureElement;
    using ElementId = PDFContentStructureElementId;
    using Child = PDFContentStructureChild;
    using Change = PDFContentStructureChange;
    using ParentTreeItem = PDFContentParentTreeItem;

    /// Most texts of the issues of one model (a damaged document has one issue for each of its
    /// millions of elements; the last text says that more of them exist)
    static constexpr size_t MAXIMUM_ISSUE_COUNT = 100;

    /// Most null items a new marked content identifier may add at the end of an array of the
    /// parent tree. The array is indexed by the identifier (ISO 32000-2, 14.7.5.4), so a content
    /// with one identifier 2 000 000 000 would make an array of that length.
    static constexpr PDFInteger MAXIMUM_MARKED_CONTENT_GAP = 65536;

    /// Most steps along the role map (14.7.3, NOTE 2: chains and circles are permitted)
    static constexpr int MAXIMUM_ROLE_MAP_CHAIN = 32;

    /// Most items one reading stores: the children of the elements (also the foreign ones) and
    /// the items of the parent tree (zadani_overeni.md 6.2, the limit of the structure elements).
    /// The walks read each object once, but an indirect /K array listed by many elements, or an
    /// array of the parent tree listed by many keys, is stored for each of them: without one
    /// budget for the whole reading the model would cost the product of the two counts.
    static constexpr qint64 MAXIMUM_STORED_ITEMS = PDFObjectWalk::DEFAULT_MAXIMUM_NODES;

    /// State of one reading
    struct Reader
    {
        explicit Reader(const PDFObjectStorage* storage, const PDFOperationControl* control, Model* model) :
            storage(storage),
            control(control),
            model(model),
            walk(Model::MAXIMUM_DEPTH)
        {

        }

        const PDFObjectStorage* storage;
        const PDFOperationControl* control;
        Model* model;
        PDFObjectWalk walk;                                     ///< The one visited set of the elements
        std::map<PDFObjectReference, ElementId> elementIds;
        std::map<QByteArray, QByteArray> roleMap;
        std::map<QByteArray, QByteArray> standardTypes;         ///< Type -> the result of the role map
        std::set<PDFObjectReference> readOwners;                ///< Pages and streams, whose /StructParents was looked for
        std::vector<std::pair<ElementId, int>> pending;         ///< Elements to read, with their depth
        qint64 storedItemCount = 0;                             ///< Spent of MAXIMUM_STORED_ITEMS
        bool isStoredItemLimitReached = false;                  ///< Something was not stored: the model is incomplete
    };

    /// Spends the budget of the stored items for \p count items, which are stored together (the
    /// items of one array: an array is stored whole or not at all).
    /// \returns false, when the budget does not have them (then nothing more is stored)
    static bool spendStoredItems(Reader& reader, qint64 count)
    {
        if (reader.isStoredItemLimitReached || count > MAXIMUM_STORED_ITEMS - reader.storedItemCount)
        {
            reader.isStoredItemLimitReached = true;
            return false;
        }

        reader.storedItemCount += count;
        return true;
    }

    static bool isMarkedContentId(PDFInteger mcid)
    {
        return mcid >= 0 && mcid <= Model::MAXIMUM_MARKED_CONTENT_ID;
    }

    static bool isContentKind(const Child& child)
    {
        return child.kind == Child::Kind::MarkedContent || child.kind == Child::Kind::MarkedContentReference;
    }

    static void addIssue(Model& model, const QString& text)
    {
        if (model.m_issues.size() < MAXIMUM_ISSUE_COUNT)
        {
            model.m_issues.push_back(text);
        }
        else if (model.m_issues.size() == MAXIMUM_ISSUE_COUNT)
        {
            model.m_issues.push_back(PDFTranslationContext::tr("More problems of the structure were found; they are not listed."));
        }
    }

    // -------------------------------------------------------------------------------------------
    //  Reading
    // -------------------------------------------------------------------------------------------

    /// Reads the structure of the document into the empty model.
    /// \returns false, when the reading was canceled
    static bool read(const PDFDocument* document, const PDFOperationControl* control, Model& model)
    {
        const PDFObjectStorage* storage = &document->getStorage();
        const PDFDictionary* trailer = storage->getDictionaryFromObject(storage->getTrailerDictionary());
        const PDFDictionary* catalog = trailer ? storage->getDictionaryFromObject(trailer->get("Root")) : nullptr;
        if (!catalog)
        {
            return true;
        }

        const PDFObject rootValue = catalog->get("StructTreeRoot");
        const PDFObject& rootObject = storage->getObject(rootValue);
        if (!rootObject.isDictionary())
        {
            return true;
        }
        const PDFDictionary* rootDictionary = rootObject.getDictionary();

        Reader reader(storage, control, &model);

        if (const PDFDictionary* roleMap = storage->getDictionaryFromObject(rootDictionary->get("RoleMap")))
        {
            for (size_t i = 0; i < roleMap->getCount(); ++i)
            {
                const PDFObject& value = storage->getObject(roleMap->getValue(i));
                if (value.isName())
                {
                    reader.roleMap.emplace(roleMap->getKey(i).getString(), value.getString());
                }
            }
        }

        // The root is the element 0 of the model, but no structure element: it is not among the
        // elements a value of the parent tree can name (such a value is kept as a foreign one)
        Element root;
        root.id = 0;
        if (rootValue.isReference())
        {
            root.reference = rootValue.getReference();
            reader.walk.enterReference(root.reference, 0);
        }
        model.m_structureTreeRoot = root.reference;
        model.m_elements.push_back(std::move(root));
        reader.pending.emplace_back(ElementId(0), 0);

        // Depth first, in the order of the document, without recursion: the element listed by
        // two parents belongs to the first of them in this order. When the budget of the stored
        // items is spent, the rest is not read.
        while (!reader.pending.empty() && !reader.isStoredItemLimitReached)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                return false;
            }

            const ElementId id = reader.pending.back().first;
            const int depth = reader.pending.back().second;
            reader.pending.pop_back();

            // An element is a dictionary of the storage: it was checked, when the element was found
            const PDFDictionary* dictionary = id == 0 ? rootDictionary : storage->getObject(model.m_elements[size_t(id)].reference).getDictionary();
            readElement(reader, id, depth, dictionary);
        }

        if (reader.walk.isLimitReached() || reader.isStoredItemLimitReached)
        {
            model.m_isComplete = false;
            addIssue(model, PDFTranslationContext::tr("The structure tree is deeper than %1 levels or too large. The rest of it was not read and the tagged content cannot be changed.").arg(Model::MAXIMUM_DEPTH));
        }

        return readParentTree(reader, rootDictionary) && readContent(reader);
    }

    static void readElement(Reader& reader, ElementId id, int depth, const PDFDictionary* dictionary)
    {
        Model& model = *reader.model;
        const bool isRoot = id == 0;

        PDFDictionaryBuilder otherEntries;
        PDFObject kids;

        {
            Element& element = model.m_elements[size_t(id)];
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                const PDFInplaceOrMemoryString& key = dictionary->getKey(i);
                const PDFObject& value = dictionary->getValue(i);

                if (key == "K")
                {
                    kids = value;
                    continue;
                }

                // The parent of an element is the element, which lists it: /P is written from the tree
                if (!isRoot && (key == "P" || readEntry(reader, element, key, value)))
                {
                    continue;
                }

                otherEntries.addEntry(key, value);
            }

            element.otherEntries = PDFObject::createDictionary(std::move(otherEntries));

            if (!isRoot)
            {
                // /RoleMap of the root is the map of the default namespace; the types of another
                // namespace are mapped by /RoleMapNS of the namespace (14.7.3), which is not read
                element.standardType = dictionary->hasKey("NS") ? element.type : getStandardType(reader, element.type);
            }
        }

        // The elements found here are read in their order, before the later children of the parents
        const size_t firstPending = reader.pending.size();
        readChildren(reader, id, depth, kids);
        std::reverse(reader.pending.begin() + firstPending, reader.pending.end());
    }

    /// Reads an entry of a structure element (Table 355), which the model keeps as a value.
    /// \returns false for another key and for a value of a wrong type (kept among the other entries)
    static bool readEntry(Reader& reader, Element& element, const PDFInplaceOrMemoryString& key, const PDFObject& value)
    {
        const PDFObject& object = reader.storage->getObject(value);

        if (key == "S")
        {
            if (object.isName())
            {
                element.type = object.getString();
                return true;
            }
            return false;
        }

        if (key == "ID")
        {
            if (object.isString())
            {
                element.elementId = object.getString();
                return true;
            }
            return false;
        }

        if (key == "Pg")
        {
            if (value.isReference())
            {
                element.page = value.getReference();
                return true;
            }
            return false;
        }

        std::optional<QString>* text = nullptr;
        if (key == "T")
        {
            text = &element.title;
        }
        else if (key == "Alt")
        {
            text = &element.alternateText;
        }
        else if (key == "ActualText")
        {
            text = &element.actualText;
        }
        else if (key == "E")
        {
            text = &element.expandedText;
        }
        else if (key == "Lang")
        {
            text = &element.language;
        }

        if (text && object.isString())
        {
            *text = PDFEncoding::convertTextString(object.getString());
            return true;
        }

        return false;
    }

    /// The type an element has by the role map (ISO 32000-2, 14.7.3): a type is always mapped to
    /// its name in the role map, also a standard one; the chain is followed, until it comes to a
    /// standard type, to a type without a mapping, or back to a type it has met (the type then
    /// stays as it is written: nothing it could be taken for was found).
    static QByteArray getStandardType(Reader& reader, const QByteArray& type)
    {
        auto itCached = reader.standardTypes.find(type);
        if (itCached != reader.standardTypes.end())
        {
            return itCached->second;
        }

        QByteArray result = type;
        QByteArray current = type;
        std::set<QByteArray> met = { type };

        for (int step = 0; ; ++step)
        {
            auto it = reader.roleMap.find(current);
            if (it == reader.roleMap.end())
            {
                result = current;
                break;
            }

            current = it->second;
            if (PDFStructureItem::getTypeFromName(current) != PDFStructureItem::Invalid)
            {
                result = current;
                break;
            }

            if (!met.insert(current).second || step + 1 >= MAXIMUM_ROLE_MAP_CHAIN)
            {
                break;
            }
        }

        reader.standardTypes.emplace(type, result);
        return result;
    }

    static void readChildren(Reader& reader, ElementId id, int depth, const PDFObject& kidsValue)
    {
        const PDFObject& kids = reader.storage->getObject(kidsValue);
        if (kids.isNull())
        {
            return;
        }

        // Each item becomes one child or one foreign child
        if (kids.isArray())
        {
            if (!spendStoredItems(reader, qint64(kids.getArray()->getCount())))
            {
                return;
            }

            for (const PDFObject& item : *kids.getArray())
            {
                readChild(reader, id, depth, item);
            }
        }
        else
        {
            if (!spendStoredItems(reader, 1))
            {
                return;
            }

            reader.model->m_singleChildElements.insert(id);
            readChild(reader, id, depth, kidsValue);
        }
    }

    static void readChild(Reader& reader, ElementId id, int depth, const PDFObject& item)
    {
        Model& model = *reader.model;
        const PDFObject& object = reader.storage->getObject(item);
        const PDFInteger parentNumber = model.m_elements[size_t(id)].reference.objectNumber;

        Child child;

        if (object.isInt())
        {
            child.kind = Child::Kind::MarkedContent;
            child.mcid = object.getInteger();
            addChild(reader, id, child);
            return;
        }

        if (object.isDictionary())
        {
            const PDFDictionary* dictionary = object.getDictionary();
            const PDFObject& type = reader.storage->getObject(dictionary->get("Type"));
            const QByteArray typeName = type.isName() ? type.getString() : QByteArray();

            if (typeName == "MCR")
            {
                if (readMarkedContentReference(reader, dictionary, child))
                {
                    addChild(reader, id, child);
                    return;
                }
            }
            else if (typeName == "OBJR")
            {
                if (readObjectReference(dictionary, child))
                {
                    model.m_elements[size_t(id)].children.push_back(child);
                    return;
                }
            }
            else if ((type.isNull() || typeName == "StructElem") && item.isReference())
            {
                // Table 355, /K: a dictionary without /Type is a structure element. An element
                // is an indirect object here: its children and the parent tree refer to it
                // (/P "shall be an indirect reference").
                const PDFObjectReference reference = item.getReference();
                if (reader.walk.enterReference(reference, depth + 1))
                {
                    Element element;
                    element.id = ElementId(model.m_elements.size());
                    element.parent = id;
                    element.reference = reference;
                    reader.elementIds[reference] = element.id;
                    reader.pending.emplace_back(element.id, depth + 1);

                    child.kind = Child::Kind::Element;
                    child.element = element.id;
                    model.m_elements.push_back(std::move(element));
                    model.m_elements[size_t(id)].children.push_back(child);
                    return;
                }

                if (reader.walk.isVisited(reference))
                {
                    addIssue(model, PDFTranslationContext::tr("The structure element %1 is a child of more than one element or of itself. It is kept at its first parent.").arg(reference.objectNumber));
                }

                addForeignChild(model, id, item);
                return;
            }
        }

        addIssue(model, PDFTranslationContext::tr("A child of the structure element %1 is not a structure element, a marked content or an object reference. It is kept unchanged.").arg(parentNumber));
        addForeignChild(model, id, item);
    }

    static void addChild(Reader& reader, ElementId id, const Child& child)
    {
        Model& model = *reader.model;
        Element& element = model.m_elements[size_t(id)];

        if (!isMarkedContentId(child.mcid))
        {
            addIssue(model, PDFTranslationContext::tr("The marked content identifier %1 of the structure element %2 is out of range.").arg(child.mcid).arg(element.reference.objectNumber));
        }

        element.children.push_back(child);
    }

    static void addForeignChild(Model& model, ElementId id, const PDFObject& item)
    {
        Model::ForeignChild foreignChild;
        foreignChild.position = model.m_elements[size_t(id)].children.size();
        foreignChild.object = item;
        model.m_foreignChildren[id].push_back(std::move(foreignChild));
    }

    /// Reads a marked-content reference (Table 357). A dictionary with another key or with a value
    /// of a wrong type is not read: it could not be written back as it is.
    static bool readMarkedContentReference(Reader& reader, const PDFDictionary* dictionary, Child& child)
    {
        bool hasIdentifier = false;
        child.kind = Child::Kind::MarkedContentReference;

        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            const PDFInplaceOrMemoryString& key = dictionary->getKey(i);
            const PDFObject& value = dictionary->getValue(i);

            if (key == "Type")
            {
                continue;
            }

            if (key == "MCID")
            {
                const PDFObject& identifier = reader.storage->getObject(value);
                if (!identifier.isInt())
                {
                    return false;
                }
                child.mcid = identifier.getInteger();
                hasIdentifier = true;
                continue;
            }

            PDFObjectReference* reference = nullptr;
            if (key == "Pg")
            {
                reference = &child.page;
            }
            else if (key == "Stm")
            {
                reference = &child.stream;
            }
            else if (key == "StmOwn")
            {
                reference = &child.streamOwner;
            }

            if (!reference || !value.isReference() || !value.getReference().isValid())
            {
                return false;
            }
            *reference = value.getReference();
        }

        return hasIdentifier;
    }

    /// Reads an object reference (Table 358), under the rule of readMarkedContentReference
    static bool readObjectReference(const PDFDictionary* dictionary, Child& child)
    {
        child.kind = Child::Kind::ObjectReference;

        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            const PDFInplaceOrMemoryString& key = dictionary->getKey(i);
            const PDFObject& value = dictionary->getValue(i);

            if (key == "Type")
            {
                continue;
            }

            PDFObjectReference* reference = nullptr;
            if (key == "Pg")
            {
                reference = &child.page;
            }
            else if (key == "Obj")
            {
                reference = &child.object;
            }

            if (!reference || !value.isReference() || !value.getReference().isValid())
            {
                return false;
            }
            *reference = value.getReference();
        }

        return child.object.isValid();
    }

    /// \returns false, when the reading was canceled
    static bool readParentTree(Reader& reader, const PDFDictionary* rootDictionary)
    {
        Model& model = *reader.model;
        const PDFObjectStorage* storage = reader.storage;

        // The entries in the order of the tree, the values not dereferenced. A parent tree read
        // in part must never be written back: a walk stopped by a limit makes the model incomplete.
        bool isLimitReached = false;
        const std::vector<std::pair<PDFInteger, PDFObject>> entries = PDFObjectUtils::readNumberTree(storage, rootDictionary->get("ParentTree"), Model::MAXIMUM_NUMBER_TREE_DEPTH, &isLimitReached);

        // The budget of the stored items is shared with the children of the elements: when it is
        // spent, the rest of the parent tree is not read
        const bool isStoredItemLimitReachedBefore = reader.isStoredItemLimitReached;

        for (const auto& entry : entries)
        {
            if (PDFOperationControl::isOperationCanceled(reader.control))
            {
                return false;
            }

            if (reader.isStoredItemLimitReached)
            {
                break;
            }

            const PDFInteger key = entry.first;
            const PDFObject& value = entry.second;

            // A key has one value (7.9.7): the first one in the order of the tree is kept
            if (model.m_parentTree.count(key))
            {
                addIssue(model, PDFTranslationContext::tr("The key %1 is more than once in the parent tree of the structure. Its first value is used.").arg(key));
                continue;
            }

            ParentTreeItem item;
            item.raw = value;

            const PDFObject& object = storage->getObject(value);
            if (object.isArray())
            {
                // A page or a content stream: the array is indexed by the marked content identifier
                // (14.7.5.4), so every item keeps its index, whatever it is
                const PDFArray* array = object.getArray();
                if (!spendStoredItems(reader, qint64(array->getCount())))
                {
                    break;
                }

                item.kind = ParentTreeItem::Kind::MarkedContentArray;
                item.elements.reserve(array->getCount());

                for (size_t i = 0; i < array->getCount(); ++i)
                {
                    const PDFObject& arrayItem = array->getItem(i);
                    ElementId element = -1;

                    if (arrayItem.isReference())
                    {
                        auto it = reader.elementIds.find(arrayItem.getReference());
                        if (it != reader.elementIds.end())
                        {
                            element = it->second;
                        }
                    }

                    if (element < 0 && !storage->getObject(arrayItem).isNull())
                    {
                        model.m_foreignParentTreeItems.emplace(key, PDFInteger(i));
                    }

                    item.elements.push_back(element);
                }
            }
            else if (value.isReference())
            {
                auto it = reader.elementIds.find(value.getReference());
                if (it != reader.elementIds.end())
                {
                    if (!spendStoredItems(reader, 1))
                    {
                        break;
                    }

                    item.kind = ParentTreeItem::Kind::Element;
                    item.elements.push_back(it->second);
                }
            }

            model.m_parentTree.emplace(key, std::move(item));
        }

        if (isLimitReached || (reader.isStoredItemLimitReached && !isStoredItemLimitReachedBefore))
        {
            model.m_isComplete = false;
            addIssue(model, PDFTranslationContext::tr("The parent tree of the structure is nested deeper than %1 levels or too large. The rest of it was not read and the tagged content cannot be changed.").arg(Model::MAXIMUM_NUMBER_TREE_DEPTH));
        }

        const PDFObject& nextKey = storage->getObject(rootDictionary->get("ParentTreeNextKey"));
        model.m_nextParentTreeKey = nextKey.isInt() ? nextKey.getInteger() : 0;
        return true;
    }

    /// Reads /StructParents of a page or of a content stream a child names (Table 359)
    static void readStructuralParentKey(Reader& reader, PDFObjectReference owner)
    {
        if (!owner.isValid() || !reader.readOwners.insert(owner).second)
        {
            return;
        }

        if (const PDFDictionary* dictionary = reader.storage->getDictionaryFromObject(reader.storage->getObject(owner)))
        {
            const PDFObject& key = reader.storage->getObject(dictionary->get("StructParents"));
            if (key.isInt() && key.getInteger() >= 0)
            {
                reader.model->m_structuralParentKeys[owner] = key.getInteger();
            }
        }
    }

    /// The page or the content stream of a marked content child: /Stm of a marked-content
    /// reference, else its /Pg, else /Pg of the element (Tables 355 and 357)
    static PDFObjectReference getContentOwner(const Element& element, const Child& child)
    {
        if (child.stream.isValid())
        {
            return child.stream;
        }

        return child.page.isValid() ? child.page : element.page;
    }

    /// The key of the parent tree of a marked content child, -1 when it is not known
    static PDFInteger getStructuralParentKey(const Model& model, const Element& element, const Child& child)
    {
        auto it = model.m_structuralParentKeys.find(getContentOwner(element, child));
        return it != model.m_structuralParentKeys.end() ? it->second : -1;
    }

    /// Compares the marked content children of the elements with the parent tree.
    /// \returns false, when the reading was canceled
    static bool readContent(Reader& reader)
    {
        Model& model = *reader.model;
        std::map<Model::ContentKey, ElementId> firstElements;

        for (size_t i = 0; i < model.m_elements.size(); ++i)
        {
            if (PDFOperationControl::isOperationCanceled(reader.control))
            {
                return false;
            }

            const Element& element = model.m_elements[i];
            readForeignContent(reader, element);

            for (const Child& child : element.children)
            {
                if (!isContentKind(child) || !isMarkedContentId(child.mcid))
                {
                    continue;
                }

                readStructuralParentKey(reader, getContentOwner(element, child));
                const PDFInteger key = getStructuralParentKey(model, element, child);
                if (key < 0)
                {
                    continue;
                }

                auto itLargest = model.m_largestChildContentId.emplace(key, child.mcid).first;
                itLargest->second = qMax(itLargest->second, child.mcid);

                const Model::ContentKey contentKey(key, child.mcid);
                if (!firstElements.emplace(contentKey, element.id).second)
                {
                    if (model.m_irregularContent.insert(contentKey).second)
                    {
                        addIssue(model, PDFTranslationContext::tr("The marked content %1 of the structure key %2 is a child of a structure element more than once.").arg(child.mcid).arg(key));
                    }
                }
                else if (model.getElementOfMarkedContent(key, child.mcid) != element.id)
                {
                    model.m_irregularContent.insert(contentKey);
                    addIssue(model, PDFTranslationContext::tr("The marked content %1 of the structure key %2 has another structure element in the parent tree than in the structure tree.").arg(child.mcid).arg(key));
                }
            }
        }

        // An item of the parent tree, whose element does not list the content (the element the
        // parent tree names takes also its child, whose page is not known, for the content: the
        // sorted MCIDs of such children of an element are collected, when they are needed first)
        std::map<ElementId, std::vector<PDFInteger>> unknownKeyMcids;
        for (const auto& itemOfKey : model.m_parentTree)
        {
            if (PDFOperationControl::isOperationCanceled(reader.control))
            {
                return false;
            }

            const ParentTreeItem& item = itemOfKey.second;
            if (item.kind != ParentTreeItem::Kind::MarkedContentArray)
            {
                continue;
            }

            for (size_t i = 0; i < item.elements.size(); ++i)
            {
                const ElementId element = item.elements[i];
                if (element < 0)
                {
                    continue;
                }

                const Model::ContentKey contentKey(itemOfKey.first, PDFInteger(i));
                auto it = firstElements.find(contentKey);
                if (it != firstElements.end() && it->second == element)
                {
                    continue;
                }

                auto itUnknown = unknownKeyMcids.find(element);
                if (itUnknown == unknownKeyMcids.end())
                {
                    itUnknown = unknownKeyMcids.emplace(element, getUnknownKeyMcids(model, element)).first;
                }

                if (!std::binary_search(itUnknown->second.cbegin(), itUnknown->second.cend(), PDFInteger(i)))
                {
                    model.m_irregularContent.insert(contentKey);
                }
            }
        }

        return true;
    }

    /// The sorted MCIDs of the marked content children of an element, whose page is not known
    static std::vector<PDFInteger> getUnknownKeyMcids(const Model& model, ElementId id)
    {
        std::vector<PDFInteger> result;
        if (id < 0 || size_t(id) >= model.m_elements.size())
        {
            return result;
        }

        const Element& element = model.m_elements[size_t(id)];
        for (const Child& child : element.children)
        {
            if (isContentKind(child) && isMarkedContentId(child.mcid) && getStructuralParentKey(model, element, child) < 0)
            {
                result.push_back(child.mcid);
            }
        }

        std::sort(result.begin(), result.end());
        return result;
    }

    /// A reference entry, which may be missing (\p reference stays as it is).
    /// \returns false for a value, which is no valid reference
    static bool readOptionalReference(const PDFObject& value, PDFObjectReference& reference)
    {
        if (value.isNull())
        {
            return true;
        }

        if (!value.isReference() || !value.getReference().isValid())
        {
            return false;
        }

        reference = value.getReference();
        return true;
    }

    /// The marked content a marked-content reference names (Table 357): an integer /MCID and the
    /// page or the stream of the references /Stm, /Pg, else \p defaultPage; other keys do not
    /// matter. \returns false, when it cannot be told
    static bool getContentOfReference(const PDFObjectStorage* storage, const PDFDictionary* dictionary, PDFObjectReference defaultPage, PDFObjectReference& owner, PDFInteger& mcid)
    {
        const PDFObject& identifier = storage->getObject(dictionary->get("MCID"));
        if (!identifier.isInt())
        {
            return false;
        }

        PDFObjectReference page = defaultPage;
        PDFObjectReference stream;
        if (!readOptionalReference(dictionary->get("Pg"), page) || !readOptionalReference(dictionary->get("Stm"), stream))
        {
            return false;
        }

        owner = stream.isValid() ? stream : page;
        mcid = identifier.getInteger();
        return owner.isValid();
    }

    /// The marked content a foreign child names exactly: a marked-content reference (also with
    /// other keys than those of Table 357), or a direct structure element dictionary, whose /K is
    /// one integer or one marked-content reference (alone or in an array of one item), on its own
    /// /Pg (no inheritance, as for the elements of the model).
    /// \returns false for anything else: such an item is not interpreted and stays as it is
    static bool getForeignContent(const Reader& reader, const Element& element, const PDFObject& item, PDFObjectReference& owner, PDFInteger& mcid, bool& isElement)
    {
        const PDFObjectStorage* storage = reader.storage;
        const PDFObject& object = storage->getObject(item);
        if (!object.isDictionary())
        {
            return false;
        }

        const PDFDictionary* dictionary = object.getDictionary();
        const PDFObject& type = storage->getObject(dictionary->get("Type"));
        if (type.isName() && type.getString() == "MCR")
        {
            isElement = false;
            return getContentOfReference(storage, dictionary, element.page, owner, mcid);
        }

        if (item.isReference() || !(type.isNull() || (type.isName() && type.getString() == "StructElem")))
        {
            return false;
        }

        const PDFObject& pageValue = dictionary->get("Pg");
        const PDFObjectReference page = pageValue.isReference() ? pageValue.getReference() : PDFObjectReference();

        const PDFObject* kids = &storage->getObject(dictionary->get("K"));
        if (kids->isArray())
        {
            if (kids->getArray()->getCount() != 1)
            {
                return false;
            }
            kids = &storage->getObject(kids->getArray()->getItem(0));
        }

        isElement = true;
        if (kids->isInt())
        {
            owner = page;
            mcid = kids->getInteger();
            return owner.isValid();
        }

        if (kids->isDictionary())
        {
            const PDFObject& kidType = storage->getObject(kids->getDictionary()->get("Type"));
            return kidType.isName() && kidType.getString() == "MCR" && getContentOfReference(storage, kids->getDictionary(), page, owner, mcid);
        }

        return false;
    }

    /// Finds the marked content the foreign children of an element name exactly: such content is
    /// irregular (its deletion removes them, it cannot be split)
    static void readForeignContent(Reader& reader, const Element& element)
    {
        Model& model = *reader.model;
        auto it = model.m_foreignChildren.find(element.id);
        if (it == model.m_foreignChildren.end())
        {
            return;
        }

        for (Model::ForeignChild& foreignChild : it->second)
        {
            PDFObjectReference owner;
            PDFInteger mcid = -1;
            bool isElement = false;
            if (!getForeignContent(reader, element, foreignChild.object, owner, mcid, isElement) || !isMarkedContentId(mcid))
            {
                continue;
            }

            readStructuralParentKey(reader, owner);
            auto itKey = model.m_structuralParentKeys.find(owner);
            if (itKey == model.m_structuralParentKeys.end())
            {
                continue;
            }

            foreignChild.key = itKey->second;
            foreignChild.mcid = mcid;
            foreignChild.isElement = isElement;
            model.m_irregularContent.insert(Model::ContentKey(itKey->second, mcid));
        }
    }

    // -------------------------------------------------------------------------------------------
    //  Consistency
    // -------------------------------------------------------------------------------------------

    /// The array of a page or of a content stream in the parent tree, nullptr when the key has none
    static const ParentTreeItem* getContentArray(const Model& model, PDFInteger key)
    {
        if (key < 0)
        {
            return nullptr;
        }

        auto it = model.m_parentTree.find(key);
        if (it == model.m_parentTree.end() || it->second.kind != ParentTreeItem::Kind::MarkedContentArray)
        {
            return nullptr;
        }

        return &it->second;
    }

    /// The same of a model being changed
    static ParentTreeItem* getContentArray(Model& model, PDFInteger key)
    {
        return const_cast<ParentTreeItem*>(getContentArray(const_cast<const Model&>(model), key));
    }

    static Change createChange(const Model& model, Change::Action action, PDFInteger key, PDFInteger mcid)
    {
        Change change;
        change.action = action;
        change.structuralParentKey = key;
        change.mcid = mcid;
        change.element = model.getElementOfMarkedContent(key, mcid);
        return change;
    }

    static Change createRefusal(const Model& model, PDFInteger key, PDFInteger mcid, const QString& text)
    {
        Change change = createChange(model, Change::Action::Refused, key, mcid);
        change.text = text;
        return change;
    }

    static QString getIncompleteText()
    {
        return PDFTranslationContext::tr("The structure of the document was not read whole, so the tagged content cannot be changed.");
    }

    /// The last content of a sequence is gone
    static Change createRemoval(const Model& model, PDFInteger key, PDFInteger mcid)
    {
        if (!model.m_isComplete)
        {
            return createRefusal(model, key, mcid, getIncompleteText());
        }

        return createChange(model, Change::Action::RemoveSequence, key, mcid);
    }

    /// A part of a sequence gets a new identifier of the same element
    static Change createSplit(const Model& model, PDFInteger key, PDFInteger mcid, PDFInteger largestUsedInContent)
    {
        if (!model.m_isComplete)
        {
            return createRefusal(model, key, mcid, getIncompleteText());
        }

        // The rest gets a new identifier of the one element, which lists the sequence, as its
        // child right behind the child of the sequence. A sequence, which the parent tree does not
        // tie to that one element, cannot be tagged anew (zadani_overeni.md 6.2): no element
        // would list the rest, or the parent tree would name an element without such a child.
        if (model.m_irregularContent.count(Model::ContentKey(key, mcid)))
        {
            return createRefusal(model, key, mcid, PDFTranslationContext::tr("The structure of the document does not list this tagged content consistently, so a part of it cannot be tagged separately."));
        }

        const PDFInteger newMcid = model.allocateMarkedContentId(key, qMax(largestUsedInContent, mcid));
        if (newMcid < 0)
        {
            return createRefusal(model, key, mcid, PDFTranslationContext::tr("The page has no free marked content identifier for a part of the tagged content."));
        }

        Change change = createChange(model, Change::Action::SplitSequence, key, mcid);
        change.newMcid = newMcid;
        return change;
    }

    /// The child is the marked content \p mcid of the key. A child, whose page is not known, is
    /// taken for it only in the element the parent tree names (\p isUnknownKeyAccepted).
    static bool isContentChild(const Model& model, const Element& element, const Child& child, PDFInteger key, PDFInteger mcid, bool isUnknownKeyAccepted)
    {
        if (!isContentKind(child) || child.mcid != mcid)
        {
            return false;
        }

        const PDFInteger childKey = getStructuralParentKey(model, element, child);
        return childKey == key || (isUnknownKeyAccepted && childKey < 0);
    }

    static void removeChild(Model& model, Element& element, size_t index)
    {
        element.children.erase(element.children.begin() + index);

        auto it = model.m_foreignChildren.find(element.id);
        if (it != model.m_foreignChildren.end())
        {
            for (Model::ForeignChild& foreignChild : it->second)
            {
                if (foreignChild.position > index)
                {
                    --foreignChild.position;
                }
            }
        }

        element.isModified = true;
        model.m_isModified = true;
    }

    /// Inserts a child in front of the child \p index (and of the foreign children there)
    static void insertChild(Model& model, Element& element, size_t index, const Child& child)
    {
        element.children.insert(element.children.begin() + index, child);

        auto it = model.m_foreignChildren.find(element.id);
        if (it != model.m_foreignChildren.end())
        {
            for (Model::ForeignChild& foreignChild : it->second)
            {
                if (foreignChild.position >= index)
                {
                    ++foreignChild.position;
                }
            }
        }

        element.isModified = true;
        model.m_isModified = true;
    }

    /// The children of the removed sequences, which one element loses
    struct ElementRemoval
    {
        std::set<Model::ContentKey> contents;   ///< (key, MCID): a child of the key
        std::set<PDFInteger> unknownKeyMcids;   ///< MCID: also a child, whose page is not known
    };

    /// The children the RemoveSequence changes of a command remove, collected first and removed in
    /// one pass over the children of each element (Clear Page gives a change for each sequence
    /// of a page: a pass for each change would cost their product). The result is the same as of
    /// the changes one at a time: a child is removed, when any of them names it, and a foreign
    /// child keeps its place among the children that stay.
    struct RemovalBatch
    {
        std::map<ElementId, ElementRemoval> elements;   ///< The element the parent tree names
        std::set<Model::ContentKey> irregularContents;  ///< Removed from every element, which lists it
    };

    /// The child is named by a removal of the batch
    static bool isRemovedChild(const Model& model, const Element& element, const Child& child, const ElementRemoval* removal, const std::set<Model::ContentKey>& irregularContents)
    {
        if (!isContentKind(child))
        {
            return false;
        }

        const PDFInteger childKey = getStructuralParentKey(model, element, child);
        if (childKey < 0)
        {
            // A child, whose page is not known, is taken for the content only in the element the
            // parent tree names
            return removal && removal->unknownKeyMcids.count(child.mcid);
        }

        const Model::ContentKey contentKey(childKey, child.mcid);
        return (removal && removal->contents.count(contentKey)) || irregularContents.count(contentKey);
    }

    /// Removes the children of an element, which the batch names, in one pass
    static void removeContentChildren(Model& model, Element& element, const ElementRemoval* removal, const std::set<Model::ContentKey>& irregularContents)
    {
        auto itForeign = model.m_foreignChildren.find(element.id);
        const bool hasForeignChildren = itForeign != model.m_foreignChildren.end() && !itForeign->second.empty();

        // removedInFront[i] = the removed children of the indices below i (a foreign child at the
        // position i moves in front by them)
        std::vector<size_t> removedInFront;
        if (hasForeignChildren)
        {
            removedInFront.reserve(element.children.size() + 1);
        }

        const size_t count = element.children.size();
        size_t removedCount = 0;
        size_t keptCount = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (hasForeignChildren)
            {
                removedInFront.push_back(removedCount);
            }

            if (isRemovedChild(model, element, element.children[i], removal, irregularContents))
            {
                ++removedCount;
                continue;
            }

            if (keptCount != i)
            {
                element.children[keptCount] = std::move(element.children[i]);
            }
            ++keptCount;
        }

        if (removedCount == 0)
        {
            return;
        }

        element.children.resize(keptCount);
        if (hasForeignChildren)
        {
            removedInFront.push_back(removedCount);
            for (Model::ForeignChild& foreignChild : itForeign->second)
            {
                foreignChild.position -= removedInFront[qMin(foreignChild.position, count)];
            }
        }

        element.isModified = true;
        model.m_isModified = true;
    }

    /// Removes the foreign children of an element, which name exactly a removed content (such
    /// content is irregular, readForeignContent): a marked-content reference is removed, a direct
    /// structure element loses its /K and stays with its other entries
    static void removeForeignContent(Model& model, Element& element, const std::set<Model::ContentKey>& irregularContents)
    {
        auto it = model.m_foreignChildren.find(element.id);
        if (it == model.m_foreignChildren.end())
        {
            return;
        }

        std::vector<Model::ForeignChild>& foreignChildren = it->second;
        bool isChanged = false;
        size_t keptCount = 0;
        for (size_t i = 0; i < foreignChildren.size(); ++i)
        {
            Model::ForeignChild& foreignChild = foreignChildren[i];
            if (foreignChild.key >= 0 && irregularContents.count(Model::ContentKey(foreignChild.key, foreignChild.mcid)))
            {
                isChanged = true;
                if (!foreignChild.isElement)
                {
                    continue;
                }

                // A direct dictionary (getForeignContent): its entries without /K
                PDFDictionaryBuilder entries;
                const PDFDictionary* dictionary = foreignChild.object.getDictionary();
                for (size_t j = 0; j < dictionary->getCount(); ++j)
                {
                    if (!(dictionary->getKey(j) == "K"))
                    {
                        entries.addEntry(dictionary->getKey(j), dictionary->getValue(j));
                    }
                }
                foreignChild.object = PDFObject::createDictionary(std::move(entries));
                foreignChild.key = -1;
                foreignChild.mcid = -1;
                foreignChild.isElement = false;
            }

            if (keptCount != i)
            {
                foreignChildren[keptCount] = std::move(foreignChild);
            }
            ++keptCount;
        }

        if (isChanged)
        {
            foreignChildren.resize(keptCount);
            element.isModified = true;
            model.m_isModified = true;
        }
    }

    /// Removes the children the batch collected and empties it
    static void applyRemovals(Model& model, RemovalBatch& batch)
    {
        if (batch.irregularContents.empty())
        {
            for (const auto& elementRemoval : batch.elements)
            {
                removeContentChildren(model, model.m_elements[size_t(elementRemoval.first)], &elementRemoval.second, batch.irregularContents);
            }
        }
        else
        {
            // Every element may list an irregular content: one pass over all of them
            for (Element& element : model.m_elements)
            {
                auto it = batch.elements.find(element.id);
                removeContentChildren(model, element, it != batch.elements.end() ? &it->second : nullptr, batch.irregularContents);
                removeForeignContent(model, element, batch.irregularContents);
            }
        }

        batch.elements.clear();
        batch.irregularContents.clear();
    }

    /// Row "the last content of a sequence is deleted" of the table of zadani_jadro.md 4.12: the
    /// child of the element is removed and ParentTree[key][mcid] is null; the array keeps its
    /// length (the identifier stays used: nothing gets it again, an undone deletion gets it back).
    /// The parent tree is changed at once, the children are collected in \p batch (applyRemovals).
    static void removeSequence(Model& model, const Change& change, RemovalBatch& batch)
    {
        const PDFInteger key = change.structuralParentKey;
        const PDFInteger mcid = change.mcid;
        if (!model.m_isComplete || key < 0 || !isMarkedContentId(mcid))
        {
            return;
        }

        const Model::ContentKey contentKey(key, mcid);
        ParentTreeItem* item = getContentArray(model, key);
        if (item && size_t(mcid) < item->elements.size())
        {
            const ElementId element = item->elements[size_t(mcid)];
            const bool isForeign = model.m_foreignParentTreeItems.erase(contentKey) > 0;
            if (element >= 0 || isForeign)
            {
                item->elements[size_t(mcid)] = -1;
                item->isModified = true;
                model.m_isModified = true;
            }

            if (element >= 0 && size_t(element) < model.m_elements.size())
            {
                ElementRemoval& removal = batch.elements[element];
                removal.contents.insert(contentKey);
                removal.unknownKeyMcids.insert(mcid);
            }
        }

        // The children do not agree with the parent tree: every element, which lists the content
        if (model.m_irregularContent.erase(contentKey) > 0)
        {
            batch.irregularContents.insert(contentKey);
        }
    }

    /// Rows "a node in the middle leaves its sequence" and "an order command moves a node of a
    /// sequence": the new identifier belongs to the element of the sequence, its child goes right
    /// behind the child of the sequence. A change, which does not fit the model (the identifier
    /// is not free, the sequence has no element, the element does not list it, the sequence is
    /// irregular: createSplit refuses it), changes nothing.
    static void splitSequence(Model& model, const Change& change)
    {
        const PDFInteger key = change.structuralParentKey;
        const PDFInteger mcid = change.mcid;
        const PDFInteger newMcid = change.newMcid;
        if (!model.m_isComplete || key < 0 || !isMarkedContentId(mcid) || !isMarkedContentId(newMcid) || mcid == newMcid ||
            model.m_irregularContent.count(Model::ContentKey(key, mcid)))
        {
            return;
        }

        ParentTreeItem* item = getContentArray(model, key);
        if (!item || size_t(mcid) >= item->elements.size())
        {
            return;
        }

        const ElementId elementId = item->elements[size_t(mcid)];
        if (elementId < 0 || size_t(elementId) >= model.m_elements.size())
        {
            return;
        }

        // The child of the sequence, before anything changes: the parent tree never names an
        // element for a new identifier, which the element does not list
        Element& element = model.m_elements[size_t(elementId)];
        auto itChild = std::find_if(element.children.cbegin(), element.children.cend(), [&](const Child& child) { return isContentChild(model, element, child, key, mcid, true); });
        if (itChild == element.children.cend())
        {
            return;
        }
        const size_t childIndex = size_t(itChild - element.children.cbegin());

        auto itLargest = model.m_largestChildContentId.find(key);
        if (itLargest != model.m_largestChildContentId.end() && itLargest->second >= newMcid)
        {
            return;
        }

        const PDFInteger size = PDFInteger(item->elements.size());
        if (newMcid < size)
        {
            if (item->elements[size_t(newMcid)] >= 0 || model.m_foreignParentTreeItems.count(Model::ContentKey(key, newMcid)))
            {
                return;
            }
        }
        else
        {
            if (newMcid - size > MAXIMUM_MARKED_CONTENT_GAP)
            {
                return;
            }
            item->elements.resize(size_t(newMcid) + 1, ElementId(-1));
        }

        item->elements[size_t(newMcid)] = elementId;
        item->isModified = true;
        model.m_isModified = true;

        Child child = element.children[childIndex];
        child.mcid = newMcid;
        insertChild(model, element, childIndex + 1, child);
        model.m_largestChildContentId[key] = newMcid;
    }

    // -------------------------------------------------------------------------------------------
    //  Comparison and differences (zadani_jadro.md 4.5: an undo step keeps the difference)
    // -------------------------------------------------------------------------------------------

    using Difference = PDFContentStructureDifference;
    using ForeignChildren = std::vector<Model::ForeignChild>;

    static bool isEqualChild(const Child& first, const Child& second)
    {
        return first.kind == second.kind && first.element == second.element && first.mcid == second.mcid && first.page == second.page &&
               first.stream == second.stream && first.streamOwner == second.streamOwner && first.object == second.object;
    }

    static bool isEqualElement(const Element& first, const Element& second)
    {
        return first.id == second.id && first.parent == second.parent && first.reference == second.reference && first.type == second.type &&
               first.standardType == second.standardType && first.title == second.title && first.alternateText == second.alternateText &&
               first.actualText == second.actualText && first.expandedText == second.expandedText && first.language == second.language &&
               first.elementId == second.elementId && first.page == second.page && first.isModified == second.isModified &&
               std::equal(first.children.cbegin(), first.children.cend(), second.children.cbegin(), second.children.cend(), &PDFContentStructureModelHelper::isEqualChild) &&
               first.otherEntries == second.otherEntries;
    }

    static bool isEqualForeignChild(const Model::ForeignChild& first, const Model::ForeignChild& second)
    {
        return first.position == second.position && first.key == second.key && first.mcid == second.mcid && first.isElement == second.isElement &&
               first.object == second.object;
    }

    /// The foreign children of an element, nullptr for none
    static const ForeignChildren* getForeignChildren(const Model& model, ElementId id)
    {
        auto it = model.m_foreignChildren.find(id);
        return it != model.m_foreignChildren.end() ? &it->second : nullptr;
    }

    /// No foreign children and an empty list of them are the same
    static bool isEqualForeignChildren(const ForeignChildren* first, const ForeignChildren* second)
    {
        static const ForeignChildren none;
        const ForeignChildren& firstChildren = first ? *first : none;
        const ForeignChildren& secondChildren = second ? *second : none;
        return std::equal(firstChildren.cbegin(), firstChildren.cend(), secondChildren.cbegin(), secondChildren.cend(), &PDFContentStructureModelHelper::isEqualForeignChild);
    }

    static bool isEqualParentTreeItem(const ParentTreeItem& first, const ParentTreeItem& second)
    {
        return first.kind == second.kind && first.isModified == second.isModified && first.elements == second.elements && first.raw == second.raw;
    }

    static Difference::ElementState getElementState(const Model& model, ElementId id)
    {
        Difference::ElementState state;
        if (size_t(id) < model.m_elements.size())
        {
            state.element = model.m_elements[size_t(id)];
        }
        if (const ForeignChildren* foreignChildren = getForeignChildren(model, id))
        {
            state.foreignChildren = *foreignChildren;
        }
        return state;
    }

    /// The items of \p first, which are not in \p second
    static std::set<Model::ContentKey> getSetDifference(const std::set<Model::ContentKey>& first, const std::set<Model::ContentKey>& second)
    {
        std::set<Model::ContentKey> result;
        std::set_difference(first.cbegin(), first.cend(), second.cbegin(), second.cend(), std::inserter(result, result.end()));
        return result;
    }

    /// The items of \p contents, which are in \p set
    static std::set<Model::ContentKey> getIntersection(const std::set<Model::ContentKey>& contents, const std::set<Model::ContentKey>& set)
    {
        std::set<Model::ContentKey> result;
        std::set_intersection(contents.cbegin(), contents.cend(), set.cbegin(), set.cend(), std::inserter(result, result.end()));
        return result;
    }

    /// The parts, in which the two models differ, as they are in each of them. One pass over the
    /// model (applied copies the whole model anyway); the difference holds only what differs.
    static void computeDifference(const Model& before, const Model& after, Difference& difference)
    {
        difference = Difference();

        // Elements with their foreign children (the keys of the foreign children are elements)
        const size_t elementCount = qMax(before.m_elements.size(), after.m_elements.size());
        for (size_t i = 0; i < elementCount; ++i)
        {
            const ElementId id = ElementId(i);
            const bool isElementEqual = i < before.m_elements.size() && i < after.m_elements.size() && isEqualElement(before.m_elements[i], after.m_elements[i]);
            if (!isElementEqual || !isEqualForeignChildren(getForeignChildren(before, id), getForeignChildren(after, id)))
            {
                difference.m_before.elements.emplace(id, getElementState(before, id));
                difference.m_after.elements.emplace(id, getElementState(after, id));
            }
        }

        // Items of the parent tree
        for (const auto& item : before.m_parentTree)
        {
            auto it = after.m_parentTree.find(item.first);
            if (it == after.m_parentTree.end() || !isEqualParentTreeItem(item.second, it->second))
            {
                difference.m_before.parentTree.emplace(item.first, item.second);
                difference.m_after.parentTree.emplace(item.first, it != after.m_parentTree.end() ? std::optional<ParentTreeItem>(it->second) : std::nullopt);
            }
        }
        for (const auto& item : after.m_parentTree)
        {
            if (!before.m_parentTree.count(item.first))
            {
                difference.m_before.parentTree.emplace(item.first, std::nullopt);
                difference.m_after.parentTree.emplace(item.first, item.second);
            }
        }

        // The marked contents, whose membership in a private set differs; each state keeps, which
        // of them are in its sets
        for (const std::set<Model::ContentKey>& contents : { getSetDifference(before.m_foreignParentTreeItems, after.m_foreignParentTreeItems),
                                                             getSetDifference(after.m_foreignParentTreeItems, before.m_foreignParentTreeItems),
                                                             getSetDifference(before.m_irregularContent, after.m_irregularContent),
                                                             getSetDifference(after.m_irregularContent, before.m_irregularContent) })
        {
            difference.m_touchedContents.insert(contents.cbegin(), contents.cend());
        }
        difference.m_before.foreignParentTreeItems = getIntersection(difference.m_touchedContents, before.m_foreignParentTreeItems);
        difference.m_after.foreignParentTreeItems = getIntersection(difference.m_touchedContents, after.m_foreignParentTreeItems);
        difference.m_before.irregularContent = getIntersection(difference.m_touchedContents, before.m_irregularContent);
        difference.m_after.irregularContent = getIntersection(difference.m_touchedContents, after.m_irregularContent);

        // The largest identifiers the children name
        for (const auto& largest : before.m_largestChildContentId)
        {
            auto it = after.m_largestChildContentId.find(largest.first);
            if (it == after.m_largestChildContentId.end() || it->second != largest.second)
            {
                difference.m_before.largestChildContentId.emplace(largest.first, largest.second);
                difference.m_after.largestChildContentId.emplace(largest.first, it != after.m_largestChildContentId.end() ? std::optional<PDFInteger>(it->second) : std::nullopt);
            }
        }
        for (const auto& largest : after.m_largestChildContentId)
        {
            if (!before.m_largestChildContentId.count(largest.first))
            {
                difference.m_before.largestChildContentId.emplace(largest.first, std::nullopt);
                difference.m_after.largestChildContentId.emplace(largest.first, largest.second);
            }
        }

        difference.m_before.elementCount = before.m_elements.size();
        difference.m_after.elementCount = after.m_elements.size();
        difference.m_before.nextParentTreeKey = before.m_nextParentTreeKey;
        difference.m_after.nextParentTreeKey = after.m_nextParentTreeKey;
        difference.m_before.isModified = before.m_isModified;
        difference.m_after.isModified = after.m_isModified;
    }

    /// Puts the touched parts of \p state into the model
    static void applyState(Model& model, const Difference::State& state, const std::set<Model::ContentKey>& touchedContents)
    {
        model.m_elements.resize(state.elementCount);
        for (const auto& elementState : state.elements)
        {
            const ElementId id = elementState.first;
            if (elementState.second.element && size_t(id) < model.m_elements.size())
            {
                model.m_elements[size_t(id)] = *elementState.second.element;
            }

            if (elementState.second.foreignChildren)
            {
                model.m_foreignChildren[id] = *elementState.second.foreignChildren;
            }
            else
            {
                model.m_foreignChildren.erase(id);
            }
        }

        for (const auto& item : state.parentTree)
        {
            if (item.second)
            {
                model.m_parentTree[item.first] = *item.second;
            }
            else
            {
                model.m_parentTree.erase(item.first);
            }
        }

        for (const Model::ContentKey& contentKey : touchedContents)
        {
            model.m_foreignParentTreeItems.erase(contentKey);
            model.m_irregularContent.erase(contentKey);
        }
        model.m_foreignParentTreeItems.insert(state.foreignParentTreeItems.cbegin(), state.foreignParentTreeItems.cend());
        model.m_irregularContent.insert(state.irregularContent.cbegin(), state.irregularContent.cend());

        for (const auto& largest : state.largestChildContentId)
        {
            if (largest.second)
            {
                model.m_largestChildContentId[largest.first] = *largest.second;
            }
            else
            {
                model.m_largestChildContentId.erase(largest.first);
            }
        }

        model.m_nextParentTreeKey = state.nextParentTreeKey;
        model.m_isModified = state.isModified;
    }

    /// Order of the children for a comparison as multisets (every field)
    static bool isChildLess(const Child& first, const Child& second)
    {
        return std::tie(first.kind, first.element, first.mcid, first.page, first.stream, first.streamOwner, first.object) <
               std::tie(second.kind, second.element, second.mcid, second.page, second.stream, second.streamOwner, second.object);
    }

    /// The keys of the contents, whose children of the element differ between the two states
    /// (a child removed or added; a foreign child, which named a content of the key, removed or
    /// changed). \returns false, when a change cannot be given a key: a child, which is no
    /// marked content or whose page is not known, a changed entry of the element (a change of
    /// the structure itself, stage E5), an element, which exists in one state only.
    static bool getChangedKeys(const Model& model, const Difference::ElementState& before, const Difference::ElementState& after, std::set<PDFInteger>& keys)
    {
        if (!before.element || !after.element)
        {
            return false;
        }

        const Element& first = *before.element;
        const Element& second = *after.element;
        if (first.id != second.id || first.parent != second.parent || first.reference != second.reference || first.type != second.type ||
            first.standardType != second.standardType || first.title != second.title || first.alternateText != second.alternateText ||
            first.actualText != second.actualText || first.expandedText != second.expandedText || first.language != second.language ||
            first.elementId != second.elementId || first.page != second.page || first.otherEntries != second.otherEntries)
        {
            return false;
        }

        std::vector<Child> firstChildren = first.children;
        std::vector<Child> secondChildren = second.children;
        std::sort(firstChildren.begin(), firstChildren.end(), &PDFContentStructureModelHelper::isChildLess);
        std::sort(secondChildren.begin(), secondChildren.end(), &PDFContentStructureModelHelper::isChildLess);
        std::vector<Child> changedChildren;
        std::set_symmetric_difference(firstChildren.cbegin(), firstChildren.cend(), secondChildren.cbegin(), secondChildren.cend(),
                                      std::back_inserter(changedChildren), &PDFContentStructureModelHelper::isChildLess);

        bool isKnown = true;
        for (const Child& child : changedChildren)
        {
            const PDFInteger key = isContentKind(child) ? getStructuralParentKey(model, first, child) : -1;
            if (key < 0)
            {
                isKnown = false;
                continue;
            }
            keys.insert(key);
        }

        // The foreign children, which named a content exactly (the only ones a change touches):
        // the keys of those, which are not in the second state as they were
        static const ForeignChildren none;
        const ForeignChildren& firstForeign = before.foreignChildren ? *before.foreignChildren : none;
        const ForeignChildren& secondForeign = after.foreignChildren ? *after.foreignChildren : none;
        std::multiset<std::pair<PDFInteger, PDFInteger>> firstContents;
        std::multiset<std::pair<PDFInteger, PDFInteger>> secondContents;
        for (const Model::ForeignChild& foreignChild : firstForeign)
        {
            firstContents.emplace(foreignChild.key, foreignChild.mcid);
        }
        for (const Model::ForeignChild& foreignChild : secondForeign)
        {
            secondContents.emplace(foreignChild.key, foreignChild.mcid);
        }
        std::vector<std::pair<PDFInteger, PDFInteger>> changedContents;
        std::set_symmetric_difference(firstContents.cbegin(), firstContents.cend(), secondContents.cbegin(), secondContents.cend(), std::back_inserter(changedContents));
        for (const auto& content : changedContents)
        {
            if (content.first >= 0)
            {
                keys.insert(content.first);
            }
        }

        return isKnown;
    }

    static QString getNotSeparableText()
    {
        return PDFTranslationContext::tr("A structure element lists tagged content of this page and of another page, and the changes of both are in it: the tags of the page cannot be left out alone. Undo the changes of the tags of the page instead.");
    }

    static QString getForeignChildrenText()
    {
        return PDFTranslationContext::tr("A structure element lists tagged content of this page and of another page together with items the editor does not read, and the changes of both are in it: the tags of the page cannot be left out alone. Undo the changes of the tags of the page instead.");
    }

    static QString getAmbiguousPlaceText()
    {
        return PDFTranslationContext::tr("A structure element lists tagged content of this page between tagged content of another page, which was split and partly deleted there: the place of the tags of this page among the parts is not known, so they cannot be left out alone. Undo the changes of the tags of the page instead.");
    }

    /// The child without its marked content identifier: a part made by a split is a copy of its
    /// original with another identifier (splitSequence), so the two are equal in it
    static Child getChildWithoutIdentifier(Child child)
    {
        child.mcid = -1;
        return child;
    }

    /// One segment of separateChildren: the children between two children, which both elements
    /// have (or an end). \p removed are the indices of the original children of the segment,
    /// which the current element does not have, \p inserted the indices of the current children
    /// of the segment, which the original does not have (the parts made by splits). The removed
    /// children of the keys come back at their places, the inserted children of the other keys
    /// stay. An inserted child is right behind its original (or behind another part of it) when
    /// it is made: its original is \p previousKept or one of \p removed with the same key, page
    /// and stream. A restored child goes in front of it, when it is in front of its original.
    /// \returns false, when the original of an inserted child is not known, or when restored
    ///          children lie between two children, which can be its original
    static bool appendSegment(const Model& model,
                              const Element& element,
                              const std::vector<Child>& original,
                              const std::vector<Child>& current,
                              std::optional<size_t> previousKept,
                              const std::vector<size_t>& removed,
                              const std::vector<size_t>& inserted,
                              const std::set<PDFInteger>& keys,
                              std::vector<Child>& children,
                              QString* errorText)
    {
        // Indices of the original children, which come back, ascending
        std::vector<size_t> restored;
        for (const size_t index : removed)
        {
            const PDFInteger key = getStructuralParentKey(model, element, original[index]);
            if (key < 0)
            {
                *errorText = getNotSeparableText();
                return false;
            }
            if (keys.count(key))
            {
                restored.push_back(index);
            }
        }

        // Indices of the current children, which stay
        std::vector<size_t> written;
        for (const size_t index : inserted)
        {
            const PDFInteger key = getStructuralParentKey(model, element, current[index]);
            if (key < 0)
            {
                *errorText = getNotSeparableText();
                return false;
            }
            if (!keys.count(key))
            {
                written.push_back(index);
            }
        }

        if (!restored.empty() && !written.empty())
        {
            // The places, where the original of a part can be: the first and the last original
            // child of the segment (with the kept one in front of it) equal to the part without
            // the identifier. One map per segment: the cost is linear in its children.
            std::map<Child, std::pair<size_t, size_t>, bool (*)(const Child&, const Child&)> places(&PDFContentStructureModelHelper::isChildLess);
            if (previousKept)
            {
                places.emplace(getChildWithoutIdentifier(original[*previousKept]), std::make_pair(*previousKept, *previousKept));
            }
            for (const size_t index : removed)
            {
                auto it = places.emplace(getChildWithoutIdentifier(original[index]), std::make_pair(index, index)).first;
                it->second.second = index;
            }

            size_t restoredCount = 0;
            for (const size_t index : written)
            {
                auto it = places.find(getChildWithoutIdentifier(current[index]));
                if (it == places.end())
                {
                    *errorText = getNotSeparableText();
                    return false;
                }

                // The restored children in front of the first and in front of the last possible
                // original: one number, else the place is not known; never less than the restored
                // children already in front of an earlier part (the order of /K)
                const size_t inFrontOfFirst = size_t(std::lower_bound(restored.cbegin(), restored.cend(), it->second.first) - restored.cbegin());
                const size_t inFrontOfLast = size_t(std::lower_bound(restored.cbegin(), restored.cend(), it->second.second) - restored.cbegin());
                if (inFrontOfFirst != inFrontOfLast || inFrontOfFirst < restoredCount)
                {
                    *errorText = getAmbiguousPlaceText();
                    return false;
                }

                for (; restoredCount < inFrontOfFirst; ++restoredCount)
                {
                    children.push_back(original[restored[restoredCount]]);
                }
                children.push_back(current[index]);
            }

            for (; restoredCount < restored.size(); ++restoredCount)
            {
                children.push_back(original[restored[restoredCount]]);
            }
            return true;
        }

        for (const size_t index : written)
        {
            children.push_back(current[index]);
        }
        for (const size_t index : restored)
        {
            children.push_back(original[index]);
        }
        return true;
    }

    /// The children of an element, whose children of the keys and of other keys changed, without
    /// the changes of the keys (review R2-3: a paragraph across a page break, one page skipped).
    /// The changes of the model are removals of children and parts of splits inserted right
    /// behind their originals: the children the two elements have are in the same order, the
    /// removed ones of the keys come back at their places, the inserted ones of the keys go, the
    /// rest is as it is now. Linear in the children (a map per segment, logarithmic lookups).
    /// \param model The model (the keys of the children)
    /// \param before The element in the original (getChangedKeys has returned true for the pair)
    /// \param after The element now
    /// \param keys The keys left out
    /// \param[out] children The children without the changes of the keys
    /// \param[out] errorText Why they cannot be separated: an element with foreign children (their
    ///             places among the children are not separated), a child, whose page is not
    ///             known, children, whose order changed, a part of a split, whose original is not
    ///             known or whose place among the restored children is not (appendSegment)
    static bool separateChildren(const Model& model,
                                 const Difference::ElementState& before,
                                 const Difference::ElementState& after,
                                 const std::set<PDFInteger>& keys,
                                 std::vector<Child>& children,
                                 QString* errorText)
    {
        if ((before.foreignChildren && !before.foreignChildren->empty()) || (after.foreignChildren && !after.foreignChildren->empty()))
        {
            *errorText = getForeignChildrenText();
            return false;
        }

        const Element& element = *before.element;
        const std::vector<Child>& original = before.element->children;
        const std::vector<Child>& current = after.element->children;

        std::vector<Child> sortedOriginal = original;
        std::sort(sortedOriginal.begin(), sortedOriginal.end(), &PDFContentStructureModelHelper::isChildLess);

        children.clear();
        children.reserve(original.size() + current.size());

        std::optional<size_t> previousKept;
        std::vector<size_t> removed;
        std::vector<size_t> inserted;
        size_t originalIndex = 0;
        for (size_t i = 0; i < current.size(); ++i)
        {
            const Child& child = current[i];
            if (!std::binary_search(sortedOriginal.cbegin(), sortedOriginal.cend(), child, &PDFContentStructureModelHelper::isChildLess))
            {
                inserted.push_back(i);
                continue;
            }

            // A child of both: the original children in front of it are removed
            while (originalIndex < original.size() && !isEqualChild(original[originalIndex], child))
            {
                removed.push_back(originalIndex++);
            }
            if (originalIndex == original.size())
            {
                // Not in the original behind the previous one: the order changed (no change of
                // the model does it)
                *errorText = getNotSeparableText();
                return false;
            }

            if (!appendSegment(model, element, original, current, previousKept, removed, inserted, keys, children, errorText))
            {
                return false;
            }
            children.push_back(child);
            previousKept = originalIndex++;
            removed.clear();
            inserted.clear();
        }

        for (; originalIndex < original.size(); ++originalIndex)
        {
            removed.push_back(originalIndex);
        }
        return appendSegment(model, element, original, current, previousKept, removed, inserted, keys, children, errorText);
    }

    /// PDFContentStructureModel::withoutChangesOfKeys
    static PDFContentStructureModelPointer withoutChangesOfKeys(const Model& current, const Model& original, const std::set<PDFInteger>& keys, QString* errorText)
    {
        std::shared_ptr<Model> model = std::make_shared<Model>(current);
        Difference difference;
        computeDifference(original, current, difference);
        if (keys.empty() || difference.isEmpty())
        {
            return model;
        }

        if (original.m_elements.size() != current.m_elements.size())
        {
            if (errorText)
            {
                *errorText = getNotSeparableText();
            }
            return nullptr;
        }

        // Does a key of the contents left out have a change at all
        bool isKeyChanged = false;
        for (const auto& item : difference.m_before.parentTree)
        {
            isKeyChanged = isKeyChanged || keys.count(item.first);
        }
        for (const Model::ContentKey& contentKey : difference.m_touchedContents)
        {
            isKeyChanged = isKeyChanged || keys.count(contentKey.first);
        }

        // The elements: each one goes back to the original, when its changes are all of the keys;
        // an element with changes of the keys and of other keys is separated child by child
        std::vector<ElementId> restoredElements;
        std::vector<std::pair<ElementId, std::vector<Child>>> separatedElements;
        for (const auto& elementState : difference.m_before.elements)
        {
            const ElementId id = elementState.first;
            const Difference::ElementState& afterState = difference.m_after.elements.at(id);
            std::set<PDFInteger> changedKeys;
            const bool isKnown = getChangedKeys(current, elementState.second, afterState, changedKeys);

            bool isOfKeys = false;
            bool isOfOthers = !isKnown;
            for (const PDFInteger key : changedKeys)
            {
                const bool isLeftOut = keys.count(key) > 0;
                isOfKeys = isOfKeys || isLeftOut;
                isOfOthers = isOfOthers || !isLeftOut;
            }
            isKeyChanged = isKeyChanged || isOfKeys;

            if (isOfKeys && !isOfOthers)
            {
                restoredElements.push_back(id);
            }
            else if (isOfKeys)
            {
                std::vector<Child> children;
                QString separationError = getNotSeparableText();
                if (!isKnown || !separateChildren(current, elementState.second, afterState, keys, children, &separationError))
                {
                    if (errorText)
                    {
                        *errorText = separationError;
                    }
                    return nullptr;
                }
                separatedElements.emplace_back(id, std::move(children));
            }
        }

        if (!isKeyChanged)
        {
            return model;
        }

        // A change, whose content is not known, may be of a key left out
        for (const auto& elementState : difference.m_before.elements)
        {
            std::set<PDFInteger> changedKeys;
            if (!getChangedKeys(current, elementState.second, difference.m_after.elements.at(elementState.first), changedKeys))
            {
                if (errorText)
                {
                    *errorText = getNotSeparableText();
                }
                return nullptr;
            }
        }

        // The model leaves something out: another content, another version
        model->m_version = Model::allocateVersion();

        for (const ElementId id : restoredElements)
        {
            const Difference::ElementState& state = difference.m_before.elements.at(id);
            model->m_elements[size_t(id)] = *state.element;
            if (state.foreignChildren)
            {
                model->m_foreignChildren[id] = *state.foreignChildren;
            }
            else
            {
                model->m_foreignChildren.erase(id);
            }
        }

        // The separated elements: the entries as they are (the same in both), the children without
        // the changes of the keys; neither has foreign children (separateChildren). A change of
        // another key stays in them (a removed child stays removed, a part stays), so they
        // differ from the document: modified.
        for (auto& separated : separatedElements)
        {
            Element& element = model->m_elements[size_t(separated.first)];
            element.children = std::move(separated.second);
            element.isModified = true;
        }

        for (const auto& item : difference.m_before.parentTree)
        {
            if (keys.count(item.first))
            {
                if (item.second)
                {
                    model->m_parentTree[item.first] = *item.second;
                }
                else
                {
                    model->m_parentTree.erase(item.first);
                }
            }
        }

        for (const Model::ContentKey& contentKey : difference.m_touchedContents)
        {
            if (keys.count(contentKey.first))
            {
                model->m_foreignParentTreeItems.erase(contentKey);
                model->m_irregularContent.erase(contentKey);
                if (difference.m_before.foreignParentTreeItems.count(contentKey))
                {
                    model->m_foreignParentTreeItems.insert(contentKey);
                }
                if (difference.m_before.irregularContent.count(contentKey))
                {
                    model->m_irregularContent.insert(contentKey);
                }
            }
        }

        for (const auto& largest : difference.m_before.largestChildContentId)
        {
            if (keys.count(largest.first))
            {
                if (largest.second)
                {
                    model->m_largestChildContentId[largest.first] = *largest.second;
                }
                else
                {
                    model->m_largestChildContentId.erase(largest.first);
                }
            }
        }

        // Modified: something the writer writes differs from the document
        bool isModified = false;
        for (const Element& element : model->m_elements)
        {
            isModified = isModified || element.isModified;
        }
        for (const auto& item : model->m_parentTree)
        {
            isModified = isModified || item.second.isModified;
        }
        model->m_isModified = isModified;
        return model;
    }

    // -------------------------------------------------------------------------------------------
    //  Fingerprint of the source
    // -------------------------------------------------------------------------------------------

    /// Most levels of indirect objects the walk of the fingerprint enters: an element and its /K
    /// array behind a reference are two levels, so twice the depth of the elements and a margin
    static constexpr int MAXIMUM_FINGERPRINT_DEPTH = 2 * Model::MAXIMUM_DEPTH + Model::MAXIMUM_NUMBER_TREE_DEPTH + 8;

    /// Most levels of direct objects hashed inside one object (deeper ones are one marker)
    static constexpr int MAXIMUM_HASHED_NESTING = 128;

    /// State of one computation of the fingerprint
    struct FingerprintWalk
    {
        explicit FingerprintWalk(const PDFObjectStorage* storage) :
            storage(storage),
            hash(QCryptographicHash::Sha256),
            walk(MAXIMUM_FINGERPRINT_DEPTH)
        {

        }

        const PDFObjectStorage* storage;
        QCryptographicHash hash;
        PDFObjectWalk walk;                                         ///< The one visited set of the walk
        std::deque<std::pair<PDFObjectReference, int>> pending;     ///< Entered objects to hash, with their depth
        std::set<PDFObjectReference> owners;                        ///< /Pg and /Stm: pages and streams, whose /StructParents counts
    };

    static void hashInteger(QCryptographicHash& hash, qint64 value)
    {
        // The bytes of the number in a fixed order, whatever the machine
        char bytes[8];
        quint64 unsignedValue = quint64(value);
        for (char& byte : bytes)
        {
            byte = char(unsignedValue & 0xFF);
            unsignedValue >>= 8;
        }
        hash.addData(QByteArrayView(bytes, sizeof(bytes)));
    }

    static void hashBytes(QCryptographicHash& hash, const QByteArray& bytes)
    {
        // The length first: two texts in a row never give the bytes of two other texts
        hashInteger(hash, bytes.size());
        hash.addData(QByteArrayView(bytes));
    }

    static void hashReference(QCryptographicHash& hash, PDFObjectReference reference)
    {
        hash.addData(QByteArrayView("R"));
        hashInteger(hash, reference.objectNumber);
        hashInteger(hash, reference.generation);
    }

    /// A key, whose reference leads out of the structure (a page, a content stream, an annotation
    /// or another object of the page): the reference is hashed, the object is not entered
    static bool isLeavingKey(const PDFInplaceOrMemoryString& key)
    {
        return key == "Pg" || key == "Stm" || key == "StmOwn" || key == "Obj";
    }

    /// Hashes a value; the references it holds are entered into the walk (\p isFollowed), the
    /// pages and streams of /Pg and /Stm are collected for their /StructParents
    static void hashValue(FingerprintWalk& walk, const PDFObject& object, int depth, int nesting, bool isFollowed)
    {
        QCryptographicHash& hash = walk.hash;
        if (nesting > MAXIMUM_HASHED_NESTING)
        {
            hash.addData(QByteArrayView("?"));
            return;
        }

        switch (object.getType())
        {
            case PDFObject::Type::Null:
                hash.addData(QByteArrayView("N"));
                break;

            case PDFObject::Type::Bool:
                hash.addData(QByteArrayView(object.getBool() ? "T" : "F"));
                break;

            case PDFObject::Type::Int:
                hash.addData(QByteArrayView("I"));
                hashInteger(hash, object.getInteger());
                break;

            case PDFObject::Type::Real:
                hash.addData(QByteArrayView("D"));
                hashBytes(hash, QByteArray::number(object.getReal(), 'g', 17));
                break;

            case PDFObject::Type::String:
                hash.addData(QByteArrayView("S"));
                hashBytes(hash, object.getString());
                break;

            case PDFObject::Type::Name:
                hash.addData(QByteArrayView("/"));
                hashBytes(hash, object.getString());
                break;

            case PDFObject::Type::Array:
            {
                const PDFArray* array = object.getArray();
                hash.addData(QByteArrayView("["));
                hashInteger(hash, qint64(array->getCount()));
                for (size_t i = 0; i < array->getCount(); ++i)
                {
                    hashValue(walk, array->getItem(i), depth, nesting + 1, isFollowed);
                }
                break;
            }

            case PDFObject::Type::Dictionary:
            case PDFObject::Type::Stream:
            {
                // The dictionary of a stream: its data are no part of the structure
                const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
                hash.addData(QByteArrayView(object.isStream() ? "s" : "<"));
                hashInteger(hash, qint64(dictionary->getCount()));
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    const PDFInplaceOrMemoryString& key = dictionary->getKey(i);
                    const PDFObject& value = dictionary->getValue(i);
                    hashBytes(hash, key.getString());

                    const bool isLeaving = isLeavingKey(key);
                    if (isLeaving && value.isReference() && (key == "Pg" || key == "Stm"))
                    {
                        walk.owners.insert(value.getReference());
                    }
                    hashValue(walk, value, depth, nesting + 1, isFollowed && !isLeaving);
                }
                break;
            }

            case PDFObject::Type::Reference:
            {
                const PDFObjectReference reference = object.getReference();
                hashReference(hash, reference);
                if (isFollowed && walk.walk.enterReference(reference, depth + 1))
                {
                    walk.pending.emplace_back(reference, depth + 1);
                }
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }

    /// The fingerprint of computeSourceFingerprint; an empty array, when it was canceled
    static QByteArray computeSourceFingerprint(const PDFDocument* document, const PDFOperationControl* control)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return QByteArray();
        }

        const PDFObjectStorage* storage = document ? &document->getStorage() : nullptr;
        FingerprintWalk walk(storage);
        walk.hash.addData(QByteArrayView("PDF4QT structure fingerprint 1"));

        const PDFDictionary* trailer = storage ? storage->getDictionaryFromObject(storage->getTrailerDictionary()) : nullptr;
        const PDFDictionary* catalog = trailer ? storage->getDictionaryFromObject(trailer->get("Root")) : nullptr;
        if (!catalog)
        {
            walk.hash.addData(QByteArrayView("no catalog"));
            return walk.hash.result();
        }

        // The value of the entry, then every object reachable from it in the breadth first order
        // (the same document gives the same order), except through the keys, which leave the
        // structure; each object once
        walk.hash.addData(QByteArrayView("root"));
        hashValue(walk, catalog->get("StructTreeRoot"), 0, 0, true);

        while (!walk.pending.empty())
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                return QByteArray();
            }

            const PDFObjectReference reference = walk.pending.front().first;
            const int depth = walk.pending.front().second;
            walk.pending.pop_front();

            walk.hash.addData(QByteArrayView("object"));
            hashReference(walk.hash, reference);
            hashValue(walk, storage->getObject(reference), depth, 0, true);
        }

        // /StructParents of the pages and streams (Table 359): it gives the key of their content
        walk.hash.addData(QByteArrayView("owners"));
        hashInteger(walk.hash, qint64(walk.owners.size()));
        for (const PDFObjectReference& owner : walk.owners)
        {
            hashReference(walk.hash, owner);
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObject(owner));
            hashValue(walk, dictionary ? storage->getObject(dictionary->get("StructParents")) : PDFObject(), 0, 0, false);
        }

        // A walk stopped by its limits is a fingerprint of its own
        hashInteger(walk.hash, walk.walk.isLimitReached() ? 1 : 0);
        return walk.hash.result();
    }
};

quint64 PDFContentStructureModel::allocateVersion()
{
    static std::atomic<quint64> counter{ 0 };
    return ++counter;
}

PDFContentStructureModelPointer PDFContentStructureModel::read(const PDFDocument* document, const PDFOperationControl* control)
{
    if (PDFOperationControl::isOperationCanceled(control))
    {
        return nullptr;
    }

    std::shared_ptr<PDFContentStructureModel> model = std::make_shared<PDFContentStructureModel>();
    if (document && !PDFContentStructureModelHelper::read(document, control, *model))
    {
        return nullptr;
    }

    // A walk of its own over the same objects (the reading does not hash what it reads): the
    // fingerprint is defined once, by computeSourceFingerprint
    model->m_sourceFingerprint = PDFContentStructureModelHelper::computeSourceFingerprint(document, control);
    if (model->m_sourceFingerprint.isEmpty())
    {
        return nullptr;
    }

    return model;
}

bool PDFContentStructureModel::isEmpty() const
{
    return m_elements.empty();
}

const std::vector<PDFContentStructureElement>& PDFContentStructureModel::getElements() const
{
    return m_elements;
}

const PDFContentStructureElement* PDFContentStructureModel::getElement(PDFContentStructureElementId id) const
{
    if (id < 0 || size_t(id) >= m_elements.size())
    {
        return nullptr;
    }

    return &m_elements[size_t(id)];
}

const std::map<PDFInteger, PDFContentParentTreeItem>& PDFContentStructureModel::getParentTree() const
{
    return m_parentTree;
}

PDFInteger PDFContentStructureModel::getNextParentTreeKey() const
{
    // ISO 32000-2, Table 354: "an integer greater than any key in the parent tree"
    PDFInteger nextKey = qMax<PDFInteger>(m_nextParentTreeKey, 0);
    if (!m_parentTree.empty())
    {
        const PDFInteger largestKey = m_parentTree.rbegin()->first;
        if (largestKey >= nextKey)
        {
            nextKey = largestKey < std::numeric_limits<PDFInteger>::max() ? largestKey + 1 : largestKey;
        }
    }

    return nextKey;
}

PDFContentStructureElementId PDFContentStructureModel::getElementOfMarkedContent(PDFInteger structuralParentKey, PDFInteger mcid) const
{
    const PDFContentParentTreeItem* item = PDFContentStructureModelHelper::getContentArray(*this, structuralParentKey);
    if (!item || mcid < 0 || size_t(mcid) >= item->elements.size())
    {
        return -1;
    }

    return item->elements[size_t(mcid)];
}

PDFInteger PDFContentStructureModel::allocateMarkedContentId(PDFInteger structuralParentKey, PDFInteger largestUsedInContent) const
{
    PDFInteger largest = qMax<PDFInteger>(largestUsedInContent, -1);

    // The array of the parent tree is indexed by the identifier (ISO 32000-2, 14.7.5.4): every
    // index up to its length is used, also the one of a null
    const PDFContentParentTreeItem* item = PDFContentStructureModelHelper::getContentArray(*this, structuralParentKey);
    const PDFInteger size = item ? PDFInteger(item->elements.size()) : 0;
    largest = qMax(largest, size - 1);

    // An identifier a child of an element names is used, though the array is shorter
    auto itLargest = m_largestChildContentId.find(structuralParentKey);
    if (itLargest != m_largestChildContentId.end())
    {
        largest = qMax(largest, itLargest->second);
    }

    if (largest >= MAXIMUM_MARKED_CONTENT_ID)
    {
        return -1;
    }

    const PDFInteger mcid = largest + 1;
    if (item && mcid - size > PDFContentStructureModelHelper::MAXIMUM_MARKED_CONTENT_GAP)
    {
        return -1;
    }

    return mcid;
}

const std::vector<QString>& PDFContentStructureModel::getIssues() const
{
    return m_issues;
}

std::vector<PDFInteger> PDFContentStructureModel::getReadingOrder(PDFInteger structuralParentKey) const
{
    std::vector<PDFInteger> result;
    if (m_elements.empty() || structuralParentKey < 0)
    {
        return result;
    }

    // Depth first without recursion; an element is entered once, whatever a later stage makes
    // of the tree, so the walk ends
    std::set<PDFInteger> listed;
    std::vector<bool> entered(m_elements.size(), false);
    std::vector<std::pair<PDFContentStructureElementId, size_t>> stack;
    stack.emplace_back(PDFContentStructureElementId(0), size_t(0));
    entered[0] = true;

    while (!stack.empty())
    {
        const PDFContentStructureElement& element = m_elements[size_t(stack.back().first)];
        const size_t index = stack.back().second;

        if (index >= element.children.size())
        {
            stack.pop_back();
            continue;
        }

        ++stack.back().second;
        const PDFContentStructureChild& child = element.children[index];

        if (child.kind == PDFContentStructureChild::Kind::Element)
        {
            if (child.element >= 0 && size_t(child.element) < m_elements.size() && !entered[size_t(child.element)])
            {
                entered[size_t(child.element)] = true;
                stack.emplace_back(child.element, size_t(0));
            }
        }
        else if (PDFContentStructureModelHelper::isContentKind(child) &&
                 PDFContentStructureModelHelper::isMarkedContentId(child.mcid) &&
                 PDFContentStructureModelHelper::getStructuralParentKey(*this, element, child) == structuralParentKey &&
                 listed.insert(child.mcid).second)
        {
            result.push_back(child.mcid);
        }
    }

    return result;
}

PDFContentStructureChange PDFContentStructureModel::getChangeForDelete(PDFInteger structuralParentKey, PDFInteger mcid, bool isSequenceEmptyAfterwards) const
{
    if (!isSequenceEmptyAfterwards)
    {
        return PDFContentStructureModelHelper::createChange(*this, PDFContentStructureChange::Action::None, structuralParentKey, mcid);
    }

    return PDFContentStructureModelHelper::createRemoval(*this, structuralParentKey, mcid);
}

PDFContentStructureChange PDFContentStructureModel::getChangeForMoveOut(PDFInteger structuralParentKey, PDFInteger mcid, bool isOnlyContent, bool isInMiddle, PDFInteger largestUsedInContent) const
{
    if (isOnlyContent)
    {
        // Nothing stays in the sequence: it ends as with a deletion, and the node has no tag
        return PDFContentStructureModelHelper::createRemoval(*this, structuralParentKey, mcid);
    }

    if (!isInMiddle)
    {
        // The first or the last node only leaves the wrap: the sequence goes on without it
        return PDFContentStructureModelHelper::createChange(*this, PDFContentStructureChange::Action::Untag, structuralParentKey, mcid);
    }

    return PDFContentStructureModelHelper::createSplit(*this, structuralParentKey, mcid, largestUsedInContent);
}

PDFContentStructureChange PDFContentStructureModel::getChangeForReorder(PDFInteger structuralParentKey, PDFInteger mcid, bool isOnlyContent, PDFInteger largestUsedInContent) const
{
    if (isOnlyContent)
    {
        return PDFContentStructureModelHelper::createChange(*this, PDFContentStructureChange::Action::KeepWithWrap, structuralParentKey, mcid);
    }

    return PDFContentStructureModelHelper::createSplit(*this, structuralParentKey, mcid, largestUsedInContent);
}

PDFContentStructureModelPointer PDFContentStructureModel::applied(const std::vector<PDFContentStructureChange>& changes) const
{
    return applied(changes, nullptr);
}

PDFContentStructureModelPointer PDFContentStructureModel::applied(const std::vector<PDFContentStructureChange>& changes, PDFContentStructureDifference* difference) const
{
    std::shared_ptr<PDFContentStructureModel> model = std::make_shared<PDFContentStructureModel>(*this);

    // The children of the removed sequences are removed together, before a split looks for its
    // child and at the end: the result is the one of the changes applied one at a time
    PDFContentStructureModelHelper::RemovalBatch removals;

    for (const PDFContentStructureChange& change : changes)
    {
        switch (change.action)
        {
            case PDFContentStructureChange::Action::RemoveSequence:
                PDFContentStructureModelHelper::removeSequence(*model, change, removals);
                break;

            case PDFContentStructureChange::Action::SplitSequence:
                PDFContentStructureModelHelper::applyRemovals(*model, removals);
                PDFContentStructureModelHelper::splitSequence(*model, change);
                break;

            case PDFContentStructureChange::Action::None:
            case PDFContentStructureChange::Action::Untag:
            case PDFContentStructureChange::Action::KeepWithWrap:
            case PDFContentStructureChange::Action::Refused:
                // The structure stays as it is
                break;
        }
    }

    PDFContentStructureModelHelper::applyRemovals(*model, removals);

    // A new version, also when nothing changed (a model made by applied is a version of its own)
    model->m_version = allocateVersion();

    if (difference)
    {
        PDFContentStructureModelHelper::computeDifference(*this, *model, *difference);
        difference->m_versionBefore = m_version;
        difference->m_versionAfter = model->m_version;
    }

    return model;
}

PDFContentStructureModelPointer PDFContentStructureModel::reverted(const PDFContentStructureDifference& difference) const
{
    if (m_elements.size() != difference.m_after.elementCount)
    {
        return nullptr;
    }

    std::shared_ptr<PDFContentStructureModel> model = std::make_shared<PDFContentStructureModel>(*this);
    PDFContentStructureModelHelper::applyState(*model, difference.m_before, difference.m_touchedContents);
    // The version it reproduces (a difference, which applied did not make, has none: a new one)
    model->m_version = (difference.m_versionBefore != 0) ? difference.m_versionBefore : allocateVersion();
    return model;
}

PDFContentStructureModelPointer PDFContentStructureModel::reapplied(const PDFContentStructureDifference& difference) const
{
    if (m_elements.size() != difference.m_before.elementCount)
    {
        return nullptr;
    }

    std::shared_ptr<PDFContentStructureModel> model = std::make_shared<PDFContentStructureModel>(*this);
    PDFContentStructureModelHelper::applyState(*model, difference.m_after, difference.m_touchedContents);
    model->m_version = (difference.m_versionAfter != 0) ? difference.m_versionAfter : allocateVersion();
    return model;
}

bool PDFContentStructureModel::isEqual(const PDFContentStructureModel& other) const
{
    using Helper = PDFContentStructureModelHelper;

    if (m_elements.size() != other.m_elements.size() || m_parentTree.size() != other.m_parentTree.size() ||
        m_nextParentTreeKey != other.m_nextParentTreeKey || m_issues != other.m_issues || m_structureTreeRoot != other.m_structureTreeRoot ||
        m_isModified != other.m_isModified || m_isComplete != other.m_isComplete || m_singleChildElements != other.m_singleChildElements ||
        m_foreignParentTreeItems != other.m_foreignParentTreeItems || m_structuralParentKeys != other.m_structuralParentKeys ||
        m_irregularContent != other.m_irregularContent || m_largestChildContentId != other.m_largestChildContentId ||
        m_sourceFingerprint != other.m_sourceFingerprint)
    {
        return false;
    }

    for (size_t i = 0; i < m_elements.size(); ++i)
    {
        const PDFContentStructureElementId id = PDFContentStructureElementId(i);
        if (!Helper::isEqualElement(m_elements[i], other.m_elements[i]) ||
            !Helper::isEqualForeignChildren(Helper::getForeignChildren(*this, id), Helper::getForeignChildren(other, id)))
        {
            return false;
        }
    }

    // A foreign child of an identifier, which is no element (never made by the reading)
    for (const auto& foreignChildren : m_foreignChildren)
    {
        if (size_t(foreignChildren.first) >= m_elements.size() && !Helper::isEqualForeignChildren(&foreignChildren.second, Helper::getForeignChildren(other, foreignChildren.first)))
        {
            return false;
        }
    }
    for (const auto& foreignChildren : other.m_foreignChildren)
    {
        if (size_t(foreignChildren.first) >= m_elements.size() && !Helper::isEqualForeignChildren(Helper::getForeignChildren(*this, foreignChildren.first), &foreignChildren.second))
        {
            return false;
        }
    }

    auto itOther = other.m_parentTree.cbegin();
    for (const auto& item : m_parentTree)
    {
        if (item.first != itOther->first || !Helper::isEqualParentTreeItem(item.second, itOther->second))
        {
            return false;
        }
        ++itOther;
    }

    return true;
}

PDFContentStructureModelPointer PDFContentStructureModel::withoutChangesOfKeys(const PDFContentStructureModel& original, const std::set<PDFInteger>& structuralParentKeys, QString* errorText) const
{
    return PDFContentStructureModelHelper::withoutChangesOfKeys(*this, original, structuralParentKeys, errorText);
}

bool PDFContentStructureModel::isModified() const
{
    return m_isModified;
}

const QByteArray& PDFContentStructureModel::getSourceFingerprint() const
{
    return m_sourceFingerprint;
}

QByteArray PDFContentStructureModel::computeSourceFingerprint(const PDFDocument* document, const PDFOperationControl* control)
{
    return PDFContentStructureModelHelper::computeSourceFingerprint(document, control);
}

bool PDFContentStructureDifference::isEmpty() const
{
    return m_before.elements.empty() && m_before.parentTree.empty() && m_touchedContents.empty() && m_before.largestChildContentId.empty() &&
           m_before.elementCount == m_after.elementCount && m_before.nextParentTreeKey == m_after.nextParentTreeKey && m_before.isModified == m_after.isModified;
}

size_t PDFContentStructureDifference::getElementCount() const
{
    return m_before.elements.size();
}

size_t PDFContentStructureDifference::getParentTreeItemCount() const
{
    return m_before.parentTree.size();
}

}   // namespace pdf
