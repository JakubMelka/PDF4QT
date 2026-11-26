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

// Edit Content, stage E0 (base; stage E5 extends it by package P51), package P04: the writer of a
// changed structure model (zadani_jadro.md 4.12, "Zapis").
//
// What is written, and what never:
//  - an element changed by the editor is written at its own object: the keys the model keeps as
//    values (/S, /P, /Pg, /ID, /T, /Lang, /Alt, /E, /ActualText, /K) and every other key as it
//    was read (/A, /C, /R, /Ref, /NS, unknown keys). A value, which says the same as the one in
//    the document, keeps the object of the document, so its bytes do not change;
//  - /ParentTree is built whole as a new number tree, when one of its items changed; an item,
//    which did not change, is the value it was read as (a foreign item included), a changed array
//    keeps its nulls and its foreign items at their indices;
//  - an object the model does not own is never overwritten: a changed array of the parent tree,
//    which was an indirect object, becomes a new object, and so does the root node of the new
//    number tree. A damaged document may use such an object for something else too. The old
//    nodes stay in the document unused.
//  - the check of the document comes first: with an error nothing is written.

#include "pdfstructuretreewriter.h"

#include "pdfdocumentbuilder.h"
#include "pdfencoding.h"
#include "pdfnumbertreewriter.h"

#include <map>
#include <utility>

namespace pdf
{

/// Writing of a structure model.
///
/// Threads and ownership: static functions; a Context lives on the stack of one call of
/// PDFStructureTreeWriter::write and points to the builder and to the model of that call. An
/// object of the builder, which is read while the builder is written, is held as a PDFObject
/// value (it shares the content), never by a pointer into the storage.
class PDFStructureTreeWriterHelper
{
public:
    PDFStructureTreeWriterHelper() = delete;

    using Model = PDFContentStructureModel;
    using Element = PDFContentStructureElement;
    using ElementId = PDFContentStructureElementId;
    using Child = PDFContentStructureChild;
    using ParentTreeItem = PDFContentParentTreeItem;

    struct Context
    {
        PDFDocumentBuilder* builder = nullptr;
        const Model* model = nullptr;
        std::vector<PDFObjectReference> references;     ///< The object of each element, the new ones included
    };

    /// Checks, that the document of the builder is the one the model was read from.
    /// \returns the error, empty when the model can be written
    static QString check(const Context& context)
    {
        const PDFDocumentBuilder* builder = context.builder;
        const Model& model = *context.model;

        const PDFObject catalog = builder->getObjectByReference(builder->getCatalogReference());
        if (!catalog.isDictionary())
        {
            return PDFTranslationContext::tr("The document has no catalog, the structure cannot be written.");
        }

        const PDFObject& rootValue = catalog.getDictionary()->get("StructTreeRoot");
        const Element& root = model.m_elements.front();
        const bool isSameRoot = root.reference.isValid() ? (rootValue.isReference() && rootValue.getReference() == root.reference && builder->getObjectByReference(root.reference).isDictionary())
                                                         : rootValue.isDictionary();
        if (!isSameRoot)
        {
            return PDFTranslationContext::tr("The structure tree of the document is not the one, which was edited.");
        }

        for (const Element& element : model.m_elements)
        {
            if (element.id > 0 && element.isModified && element.reference.isValid() && !builder->getObjectByReference(element.reference).isDictionary())
            {
                return PDFTranslationContext::tr("The structure element %1 is not in the document.").arg(element.reference.objectNumber);
            }
        }

        return QString();
    }

    static bool isParentTreeModified(const Model& model)
    {
        for (const auto& item : model.m_parentTree)
        {
            if (item.second.isModified)
            {
                return true;
            }
        }

        return false;
    }

    static PDFObjectReference getReference(const Context& context, ElementId id)
    {
        return (id >= 0 && size_t(id) < context.references.size()) ? context.references[size_t(id)] : PDFObjectReference();
    }

    /// Adds an object as it is (PDFDocumentBuilder::addObject removes the null entries of a dictionary)
    static PDFObjectReference addObject(const Context& context, PDFObject object)
    {
        const PDFObjectReference reference = context.builder->addObject(PDFObject());
        context.builder->setObject(reference, std::move(object));
        return reference;
    }

    /// Sets an object, which differs from the one in the document
    static void setObject(const Context& context, PDFObjectReference reference, PDFObject object)
    {
        if (context.builder->getObjectByReference(reference) != object)
        {
            context.builder->setObject(reference, std::move(object));
        }
    }

    static void addReferenceEntry(PDFDictionaryBuilder& dictionary, const char* key, PDFObjectReference reference)
    {
        if (reference.isValid())
        {
            dictionary.addEntry(PDFInplaceOrMemoryString(key), PDFObject::createReference(reference));
        }
    }

    /// A child as an item of /K (ISO 32000-2, Table 355; Table 357; Table 358)
    static PDFObject createChild(const Context& context, const Child& child)
    {
        switch (child.kind)
        {
            case Child::Kind::Element:
            {
                const PDFObjectReference reference = getReference(context, child.element);
                return reference.isValid() ? PDFObject::createReference(reference) : PDFObject();
            }

            case Child::Kind::MarkedContent:
                return PDFObject::createInteger(child.mcid);

            case Child::Kind::MarkedContentReference:
            {
                PDFDictionaryBuilder dictionary;
                dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("MCR"));
                addReferenceEntry(dictionary, "Pg", child.page);
                addReferenceEntry(dictionary, "Stm", child.stream);
                addReferenceEntry(dictionary, "StmOwn", child.streamOwner);
                dictionary.addEntry(PDFInplaceOrMemoryString("MCID"), PDFObject::createInteger(child.mcid));
                return PDFObject::createDictionary(std::move(dictionary));
            }

            case Child::Kind::ObjectReference:
            {
                PDFDictionaryBuilder dictionary;
                dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("OBJR"));
                addReferenceEntry(dictionary, "Pg", child.page);
                addReferenceEntry(dictionary, "Obj", child.object);
                return PDFObject::createDictionary(std::move(dictionary));
            }
        }

        return PDFObject();
    }

    /// The value of /K of an element: its children and its foreign children at their places.
    /// Null for no child (the key is left out: the array of Table 355 has one item or more).
    static PDFObject createChildren(const Context& context, const Element& element)
    {
        const Model& model = *context.model;

        const std::vector<Model::ForeignChild>* foreignChildren = nullptr;
        auto itForeign = model.m_foreignChildren.find(element.id);
        if (itForeign != model.m_foreignChildren.end())
        {
            foreignChildren = &itForeign->second;
        }

        std::vector<PDFObject> items;
        items.reserve(element.children.size() + (foreignChildren ? foreignChildren->size() : 0));

        size_t foreignIndex = 0;
        for (size_t i = 0; i <= element.children.size(); ++i)
        {
            const bool isBehindLast = i == element.children.size();
            while (foreignChildren && foreignIndex < foreignChildren->size() && (isBehindLast || (*foreignChildren)[foreignIndex].position <= i))
            {
                items.push_back((*foreignChildren)[foreignIndex++].object);
            }

            if (!isBehindLast)
            {
                PDFObject child = createChild(context, element.children[i]);
                if (!child.isNull())
                {
                    items.push_back(std::move(child));
                }
            }
        }

        if (items.empty())
        {
            return PDFObject();
        }

        if (items.size() == 1 && model.m_singleChildElements.count(element.id))
        {
            return items.front();
        }

        return PDFObject::createArray(PDFArrayBuilder(std::move(items)));
    }

    /// Sets an entry of an element. When the entry of the document says the same, its object
    /// stays (a reference to the value, or the bytes of a text in its encoding).
    /// \param original The dictionary of the element in the document, nullptr for a new element
    /// \param isText The value is a text string: the texts are compared, not the bytes
    static void setEntry(const Context& context, PDFDictionaryBuilder& dictionary, const PDFDictionary* original, const char* key, PDFObject value, bool isText)
    {
        if (original && original->hasKey(key))
        {
            const PDFObject& originalValue = original->get(key);
            const PDFObject& originalObject = context.builder->getObject(originalValue);

            const bool isSame = isText ? (originalObject.isString() && PDFEncoding::convertTextString(originalObject.getString()) == PDFEncoding::convertTextString(value.getString()))
                                       : (originalValue == value || originalObject == value);
            if (isSame)
            {
                dictionary.setEntry(PDFInplaceOrMemoryString(key), originalValue);
                return;
            }
        }

        dictionary.setEntry(PDFInplaceOrMemoryString(key), std::move(value));
    }

    static void setTextEntry(const Context& context, PDFDictionaryBuilder& dictionary, const PDFDictionary* original, const char* key, const std::optional<QString>& text)
    {
        if (text.has_value())
        {
            setEntry(context, dictionary, original, key, PDFObjectFactory::createTextString(*text), true);
        }
    }

    static PDFDictionaryBuilder createOtherEntries(const Element& element)
    {
        return element.otherEntries.isDictionary() ? PDFDictionaryBuilder(*element.otherEntries.getDictionary()) : PDFDictionaryBuilder();
    }

    /// The dictionary with its entries in the order of the dictionary of the document, the new
    /// keys behind them: an object, which is written again, differs from the old one only in
    /// what changed.
    /// \param original The dictionary of the document, nullptr for a new object
    static PDFObject createDictionary(const PDFDictionaryBuilder& dictionary, const PDFDictionary* original)
    {
        if (!original)
        {
            return PDFObject::createDictionary(PDFDictionaryBuilder(dictionary));
        }

        std::map<QByteArray, PDFObject> entries;
        for (const auto& entry : dictionary)
        {
            entries.emplace(entry.first.getString(), entry.second);
        }

        PDFDictionaryBuilder ordered;
        ordered.reserve(entries.size());

        for (size_t i = 0; i < original->getCount(); ++i)
        {
            auto it = entries.find(original->getKey(i).getString());
            if (it != entries.end())
            {
                ordered.addEntry(original->getKey(i), std::move(it->second));
                entries.erase(it);
            }
        }

        for (const auto& entry : dictionary)
        {
            auto it = entries.find(entry.first.getString());
            if (it != entries.end())
            {
                ordered.addEntry(entry.first, std::move(it->second));
                entries.erase(it);
            }
        }

        return PDFObject::createDictionary(std::move(ordered));
    }

    /// The dictionary of a structure element (Table 355)
    static PDFObject createElement(const Context& context, const Element& element)
    {
        // The object of the document is held as a value: the builder is written meanwhile
        const PDFObject originalObject = element.reference.isValid() ? context.builder->getObjectByReference(element.reference) : PDFObject();
        const PDFDictionary* original = originalObject.isDictionary() ? originalObject.getDictionary() : nullptr;

        PDFDictionaryBuilder dictionary = createOtherEntries(element);

        if (!original && !dictionary.hasKey("Type"))
        {
            dictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("StructElem"));
        }

        if (!element.type.isEmpty())
        {
            setEntry(context, dictionary, original, "S", PDFObject::createName(element.type), false);
        }

        const PDFObjectReference parent = getReference(context, element.parent);
        if (parent.isValid())
        {
            dictionary.setEntry(PDFInplaceOrMemoryString("P"), PDFObject::createReference(parent));
        }

        if (element.elementId.has_value())
        {
            setEntry(context, dictionary, original, "ID", PDFObject::createString(*element.elementId), false);
        }

        if (element.page.isValid())
        {
            setEntry(context, dictionary, original, "Pg", PDFObject::createReference(element.page), false);
        }

        setTextEntry(context, dictionary, original, "T", element.title);
        setTextEntry(context, dictionary, original, "Lang", element.language);
        setTextEntry(context, dictionary, original, "Alt", element.alternateText);
        setTextEntry(context, dictionary, original, "E", element.expandedText);
        setTextEntry(context, dictionary, original, "ActualText", element.actualText);

        PDFObject children = createChildren(context, element);
        if (!children.isNull())
        {
            dictionary.setEntry(PDFInplaceOrMemoryString("K"), std::move(children));
        }

        return createDictionary(dictionary, original);
    }

    /// The value of a key of the parent tree (ISO 32000-2, 14.7.5.4)
    static PDFObject createParentTreeItem(const Context& context, PDFInteger key, const ParentTreeItem& item)
    {
        if (!item.isModified)
        {
            return item.raw;
        }

        switch (item.kind)
        {
            case ParentTreeItem::Kind::MarkedContentArray:
            {
                // The array is indexed by the marked content identifier: a null and an item,
                // which is no element of the model, keep their indices
                const PDFObject original = context.builder->getObject(item.raw);
                const PDFArray* originalArray = original.isArray() ? original.getArray() : nullptr;

                PDFArrayBuilder array;
                array.reserve(item.elements.size());
                for (size_t i = 0; i < item.elements.size(); ++i)
                {
                    const PDFObjectReference reference = getReference(context, item.elements[i]);
                    if (reference.isValid())
                    {
                        array.appendItem(PDFObject::createReference(reference));
                    }
                    else if (originalArray && i < originalArray->getCount() && context.model->m_foreignParentTreeItems.count(Model::ContentKey(key, PDFInteger(i))))
                    {
                        array.appendItem(originalArray->getItem(i));
                    }
                    else
                    {
                        array.appendItem(PDFObject());
                    }
                }

                PDFObject arrayObject = PDFObject::createArray(std::move(array));
                if (item.raw.isArray())
                {
                    return arrayObject;
                }

                return PDFObject::createReference(addObject(context, std::move(arrayObject)));
            }

            case ParentTreeItem::Kind::Element:
            {
                const PDFObjectReference reference = getReference(context, item.elements.empty() ? ElementId(-1) : item.elements.front());
                return reference.isValid() ? PDFObject::createReference(reference) : item.raw;
            }

            case ParentTreeItem::Kind::Foreign:
                break;
        }

        return item.raw;
    }

    static void write(Context& context)
    {
        PDFDocumentBuilder* builder = context.builder;
        const Model& model = *context.model;

        // The objects of the new elements first: their parents and the parent tree refer to them
        context.references.reserve(model.m_elements.size());
        for (const Element& element : model.m_elements)
        {
            context.references.push_back(element.reference.isValid() ? element.reference : builder->addObject(PDFObject()));
        }

        for (const Element& element : model.m_elements)
        {
            if (element.id > 0 && (element.isModified || !element.reference.isValid()))
            {
                setObject(context, context.references[size_t(element.id)], createElement(context, element));
            }
        }

        // The root: its other keys as they were read, /K, and the parent tree, when it changed
        const Element& root = model.m_elements.front();
        const PDFObjectReference catalogReference = builder->getCatalogReference();
        const PDFObject catalog = builder->getObjectByReference(catalogReference);
        const PDFObject originalRoot = builder->getObject(catalog.getDictionary()->get("StructTreeRoot"));

        PDFDictionaryBuilder rootDictionary = createOtherEntries(root);

        PDFObject children = root.isModified ? createChildren(context, root) : originalRoot.getDictionary()->get("K");
        if (!children.isNull())
        {
            rootDictionary.setEntry(PDFInplaceOrMemoryString("K"), std::move(children));
        }

        if (isParentTreeModified(model))
        {
            std::map<PDFInteger, PDFObject> items;
            for (const auto& item : model.m_parentTree)
            {
                items.emplace(item.first, createParentTreeItem(context, item.first, item.second));
            }

            const PDFObjectReference parentTree = addObject(context, PDFNumberTreeWriter::write(builder, items));
            rootDictionary.setEntry(PDFInplaceOrMemoryString("ParentTree"), PDFObject::createReference(parentTree));
            rootDictionary.setEntry(PDFInplaceOrMemoryString("ParentTreeNextKey"), PDFObject::createInteger(model.getNextParentTreeKey()));
        }

        setObject(context, context.references.front(), createDictionary(rootDictionary, originalRoot.getDictionary()));

        if (!root.reference.isValid())
        {
            // The root was a direct dictionary of the catalog: it is an object now, as /P of
            // its children needs (Table 355: "shall be an indirect reference")
            PDFDictionaryBuilder catalogDictionary(*catalog.getDictionary());
            catalogDictionary.setEntry(PDFInplaceOrMemoryString("StructTreeRoot"), PDFObject::createReference(context.references.front()));
            builder->setObject(catalogReference, PDFObject::createDictionary(std::move(catalogDictionary)));
        }
    }
};

PDFStructureTreeWriteResult PDFStructureTreeWriter::write(PDFDocumentBuilder* builder, const PDFContentStructureModel& model)
{
    PDFStructureTreeWriteResult result;

    if (model.isEmpty() || !model.isModified())
    {
        return result;
    }

    if (!builder)
    {
        result.errors.push_back(PDFTranslationContext::tr("There is no document to write the structure into."));
        return result;
    }

    PDFStructureTreeWriterHelper::Context context;
    context.builder = builder;
    context.model = &model;

    const QString error = PDFStructureTreeWriterHelper::check(context);
    if (!error.isEmpty())
    {
        result.errors.push_back(error);
        return result;
    }

    PDFStructureTreeWriterHelper::write(context);
    result.isWritten = true;
    return result;
}

}   // namespace pdf
