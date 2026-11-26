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

// Edit Content, stage E0, package P01 (operations, resources, stream writer, ExtGState).
// Plan: GUI_overhaul/edit_tool/zadani_jadro.md 4.2 (container), 4.4 R-4 (resources of a container).
// Standard: ISO 32000-2, 7.8.3 (resource dictionaries), 7.3.7 (an entry with the value null
// is the same as no entry).

#include "pdfcontentresources.h"
#include "pdfcontentextgstate.h"
#include "pdfdocument.h"

#include <QtGlobal>

#include <utility>

#include "pdfdbgheap.h"

namespace pdf
{

// Ownership: the resources own their values (PDFObject values, implicitly shared with the
// document they were read from, and byte arrays). Nothing points into the document: the
// dictionaries of the categories are kept as objects, so write needs neither the document nor
// its storage. The data of a category are shared by the copies of the resources and copied
// when one of the copies changes them.

struct PDFContentResources::CategoryData
{
    std::vector<Entry> entries;                     ///< The original entries in the order of the dictionary, the added ones after them
    std::map<QByteArray, size_t> indices;           ///< Name -> index in entries
    std::set<QByteArray> undefinedNames;            ///< Names used by the operations without a definition
    PDFObject originalDictionary;                   ///< The dictionary of the category as it was read (dereferenced); null, when there was none
    size_t originalCount = 0;                       ///< The entries before this index existed at load
    std::map<QByteArray, quint64> freeNumberHints;  ///< Prefix -> a number: every name of the prefix with a smaller positive number is in use
    std::set<std::pair<QByteArray, size_t>> objectKeys; ///< (PDFContentExtGStateWriter::getObjectKey of the object, index in entries) of each entry, whose object is not null and has a key; valid only when hasObjectKeys
    bool hasObjectKeys = false;                     ///< objectKeys is built (by ensureObjectKeys of a copy, which changes the category)

    /// Index of the entry of a name, entries.size() when there is none
    size_t findEntry(const QByteArray& name) const
    {
        auto it = indices.find(name);
        return it != indices.cend() ? it->second : entries.size();
    }

    /// Index of the first entry, whose object is equal (PDFObject::operator==) to the object,
    /// entries.size() when there is none. An object without a key is equal to nothing. Without
    /// the index of the keys the entries are scanned: the data may be shared by the resources of
    /// a container, which are read by any thread, so a reading never builds the index.
    size_t findObject(const PDFObject& object) const
    {
        const QByteArray key = PDFContentExtGStateWriter::getObjectKey(object);
        if (object.isNull() || key.isEmpty())
        {
            return entries.size();
        }

        if (!hasObjectKeys)
        {
            for (size_t i = 0; i < entries.size(); ++i)
            {
                if (!entries[i].object.isNull() && PDFContentExtGStateWriter::getObjectKey(entries[i].object) == key)
                {
                    return i;
                }
            }

            return entries.size();
        }

        auto it = objectKeys.lower_bound(std::make_pair(key, size_t(0)));
        return (it != objectKeys.cend() && it->first == key) ? it->second : entries.size();
    }

    /// Adds the key of the object of an entry to objectKeys (or removes it); nothing, while the
    /// index is not built
    void setObjectKey(size_t index, bool isIndexed)
    {
        if (!hasObjectKeys)
        {
            return;
        }

        const PDFObject& object = entries[index].object;
        QByteArray key = PDFContentExtGStateWriter::getObjectKey(object);
        if (object.isNull() || key.isEmpty())
        {
            return;
        }

        if (isIndexed)
        {
            objectKeys.emplace(std::move(key), index);
        }
        else
        {
            objectKeys.erase(std::make_pair(key, index));
        }
    }

    /// Makes objectKeys of all entries again (the indices of the entries changed); nothing,
    /// while the index is not built
    void rebuildObjectKeys()
    {
        if (!hasObjectKeys)
        {
            return;
        }

        objectKeys.clear();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            setObjectKey(i, true);
        }
    }

    /// Builds objectKeys, when it is not built. Only for data owned by one copy of the
    /// resources (detachCategoryData): the index costs a key and a set node per entry, so it
    /// is made for a category, which a copy changes (addObject), not for every container read.
    void ensureObjectKeys()
    {
        if (!hasObjectKeys)
        {
            hasObjectKeys = true;
            rebuildObjectKeys();
        }
    }

    bool isNameUsed(const QByteArray& name) const
    {
        return indices.find(name) != indices.cend() || undefinedNames.find(name) != undefinedNames.cend();
    }

    /// The smallest positive number, which gives a free name of a prefix. The loop ends: there
    /// are at most indices.size() + undefinedNames.size() names in use.
    quint64 findFreeNumber(const QByteArray& prefix) const
    {
        quint64 number = 1;

        auto it = freeNumberHints.find(prefix);
        if (it != freeNumberHints.cend())
        {
            number = it->second;
        }

        while (isNameUsed(createName(prefix, number)))
        {
            ++number;
        }

        return number;
    }

    static QByteArray createName(const QByteArray& prefix, quint64 number)
    {
        return prefix + QByteArray::number(number);
    }
};

/// Parts of PDFContentResources, which need no access to its members
class PDFContentResourcesHelper
{
public:
    PDFContentResourcesHelper() = delete;

    static const std::vector<PDFContentResources::Entry>& getNoEntries()
    {
        static const std::vector<PDFContentResources::Entry> s_noEntries;
        return s_noEntries;
    }

    static const std::set<QByteArray>& getNoNames()
    {
        static const std::set<QByteArray> s_noNames;
        return s_noNames;
    }

    /// An object as the processor sees it: a reference is followed once
    /// (PDFPageContentProcessor::initDictionaries does the same, so both agree on what is defined)
    static const PDFObject& dereference(const PDFDocument* document, const PDFObject& object)
    {
        return document ? document->getObject(object) : object;
    }
};

const PDFContentResources::CategoryData* PDFContentResources::getCategoryData(PDFContentResourceCategory category) const
{
    const size_t index = static_cast<size_t>(category);
    return index < CATEGORY_COUNT ? m_categories[index].get() : nullptr;
}

PDFContentResources::CategoryData* PDFContentResources::detachCategoryData(PDFContentResourceCategory category)
{
    const size_t index = static_cast<size_t>(category);
    if (index >= CATEGORY_COUNT)
    {
        return nullptr;
    }

    CategoryDataPointer& data = m_categories[index];
    if (!data)
    {
        data = std::make_shared<CategoryData>();
    }
    else if (data.use_count() != 1)
    {
        // Shared data may be read by another thread just now (the resources of a container):
        // they are never changed, this copy of the resources gets its own
        data = std::make_shared<CategoryData>(*data);
    }

    return data.get();
}

PDFContentResources PDFContentResources::read(const PDFDocument* document, const PDFObject& resources, const PDFContentOperationList& operations)
{
    PDFContentResources result;

    const PDFObject& resourcesObject = PDFContentResourcesHelper::dereference(document, resources);
    if (resourcesObject.isDictionary())
    {
        result.m_originalResources = resourcesObject;

        const PDFDictionary* resourcesDictionary = resourcesObject.getDictionary();
        for (const PDFContentResourceCategory category : getCategories())
        {
            const PDFObject& categoryObject = PDFContentResourcesHelper::dereference(document, resourcesDictionary->get(getCategoryKey(category)));
            if (!categoryObject.isDictionary())
            {
                // Missing or of a wrong type: the category defines nothing
                continue;
            }

            CategoryData* data = result.detachCategoryData(category);
            data->originalDictionary = categoryObject;

            const PDFDictionary* categoryDictionary = categoryObject.getDictionary();
            data->entries.reserve(categoryDictionary->getCount());
            for (const PDFDictionary::DictionaryEntry& dictionaryEntry : *categoryDictionary)
            {
                QByteArray name = dictionaryEntry.first.getString();
                if (data->indices.find(name) != data->indices.cend())
                {
                    // A key written twice: a lookup finds the first one (PDFDictionary::get)
                    continue;
                }

                Entry entry;
                entry.name = name;
                entry.object = dictionaryEntry.second;
                entry.isOriginal = true;
                data->indices[std::move(name)] = data->entries.size();
                data->entries.push_back(std::move(entry));
            }
            // The index of the objects is not built here (most containers never add an object)
            data->originalCount = data->entries.size();
        }
    }

    const size_t operationCount = operations.getCount();
    for (size_t i = 0; i < operationCount; ++i)
    {
        for (const PDFContentResourceName& resourceName : operations.getOperation(i).getResourceNames())
        {
            const CategoryData* data = result.getCategoryData(resourceName.category);
            if (data)
            {
                const size_t entryIndex = data->findEntry(resourceName.name);
                if (entryIndex < data->entries.size() && !data->entries[entryIndex].object.isNull())
                {
                    // Defined
                    continue;
                }

                if (data->undefinedNames.find(resourceName.name) != data->undefinedNames.cend())
                {
                    continue;
                }
            }

            CategoryData* writableData = result.detachCategoryData(resourceName.category);
            if (writableData)
            {
                writableData->undefinedNames.insert(resourceName.name);
            }
        }
    }

    return result;
}

const std::vector<PDFContentResources::Entry>& PDFContentResources::getEntries(PDFContentResourceCategory category) const
{
    const CategoryData* data = getCategoryData(category);
    return data ? data->entries : PDFContentResourcesHelper::getNoEntries();
}

PDFObject PDFContentResources::getObject(PDFContentResourceCategory category, const QByteArray& name) const
{
    const CategoryData* data = getCategoryData(category);
    if (data)
    {
        const size_t index = data->findEntry(name);
        if (index < data->entries.size())
        {
            return data->entries[index].object;
        }
    }

    return PDFObject();
}

QByteArray PDFContentResources::getName(PDFContentResourceCategory category, const PDFObject& object) const
{
    const CategoryData* data = getCategoryData(category);
    if (data && !object.isNull())
    {
        // The first equal entry, by the index of the keys of the objects
        const size_t index = data->findObject(object);
        if (index < data->entries.size())
        {
            return data->entries[index].name;
        }
    }

    return QByteArray();
}

const std::set<QByteArray>& PDFContentResources::getUndefinedNames(PDFContentResourceCategory category) const
{
    const CategoryData* data = getCategoryData(category);
    return data ? data->undefinedNames : PDFContentResourcesHelper::getNoNames();
}

QByteArray PDFContentResources::allocateName(PDFContentResourceCategory category, const QByteArray& prefix) const
{
    const CategoryData* data = getCategoryData(category);
    return CategoryData::createName(prefix, data ? data->findFreeNumber(prefix) : 1);
}

QByteArray PDFContentResources::addObject(PDFContentResourceCategory category, const QByteArray& prefix, const PDFObject& object)
{
    if (object.isNull())
    {
        // A null value defines nothing (ISO 32000-2, 7.3.7)
        return QByteArray();
    }

    // The object is a resource already: its name (it may be the empty name, which is a valid
    // name, so getName cannot tell here). Shared data are looked up without a change, when
    // their index is built; otherwise the index is built in the own copy of the category.
    const CategoryData* existingData = getCategoryData(category);
    if (existingData && existingData->hasObjectKeys)
    {
        const size_t index = existingData->findObject(object);
        if (index < existingData->entries.size())
        {
            return existingData->entries[index].name;
        }
    }

    CategoryData* data = detachCategoryData(category);
    if (!data)
    {
        return QByteArray();
    }

    if (!data->hasObjectKeys)
    {
        data->ensureObjectKeys();
        const size_t index = data->findObject(object);
        if (index < data->entries.size())
        {
            return data->entries[index].name;
        }
    }

    const quint64 number = data->findFreeNumber(prefix);
    const QByteArray name = CategoryData::createName(prefix, number);

    Entry entry;
    entry.name = name;
    entry.object = object;
    entry.isOriginal = false;
    data->indices[name] = data->entries.size();
    data->entries.push_back(std::move(entry));
    data->setObjectKey(data->entries.size() - 1, true);
    data->freeNumberHints[prefix] = number + 1;

    return name;
}

bool PDFContentResources::removeAdded(PDFContentResourceCategory category, const QByteArray& name)
{
    const CategoryData* data = getCategoryData(category);
    if (!data)
    {
        return false;
    }

    const size_t index = data->findEntry(name);
    if (index >= data->entries.size() || index < data->originalCount)
    {
        // Not defined, or an entry, which existed at load: it is never removed (R-4)
        return false;
    }

    CategoryData* writableData = detachCategoryData(category);
    writableData->entries.erase(writableData->entries.begin() + index);
    writableData->indices.erase(name);
    for (size_t i = index; i < writableData->entries.size(); ++i)
    {
        writableData->indices[writableData->entries[i].name] = i;
    }

    // The entries after the removed one have new indices
    writableData->rebuildObjectKeys();

    // A name is free again: the smallest free numbers are not known anymore
    writableData->freeNumberHints.clear();

    return true;
}

bool PDFContentResources::setObject(PDFContentResourceCategory category, const QByteArray& name, const PDFObject& object)
{
    const CategoryData* data = getCategoryData(category);
    if (!data || object.isNull())
    {
        return false;
    }

    const size_t index = data->findEntry(name);
    if (index >= data->entries.size() || index < data->originalCount)
    {
        return false;
    }

    CategoryData* writableData = detachCategoryData(category);
    writableData->setObjectKey(index, false);
    writableData->entries[index].object = object;
    writableData->setObjectKey(index, true);
    return true;
}

bool PDFContentResources::isChanged() const
{
    for (const CategoryDataPointer& data : m_categories)
    {
        if (data && data->entries.size() > data->originalCount)
        {
            return true;
        }
    }

    return false;
}

QByteArray PDFContentResources::getCategoryKey(PDFContentResourceCategory category)
{
    // ISO 32000-2, 7.8.3, Table 34
    switch (category)
    {
        case PDFContentResourceCategory::Font:
            return QByteArrayLiteral("Font");

        case PDFContentResourceCategory::XObject:
            return QByteArrayLiteral("XObject");

        case PDFContentResourceCategory::ExtGState:
            return QByteArrayLiteral("ExtGState");

        case PDFContentResourceCategory::ColorSpace:
            return QByteArrayLiteral("ColorSpace");

        case PDFContentResourceCategory::Pattern:
            return QByteArrayLiteral("Pattern");

        case PDFContentResourceCategory::Shading:
            return QByteArrayLiteral("Shading");

        case PDFContentResourceCategory::Properties:
            return QByteArrayLiteral("Properties");
    }

    return QByteArray();
}

std::vector<PDFContentResourceCategory> PDFContentResources::getCategories()
{
    return { PDFContentResourceCategory::Font, PDFContentResourceCategory::XObject, PDFContentResourceCategory::ExtGState,
             PDFContentResourceCategory::ColorSpace, PDFContentResourceCategory::Pattern, PDFContentResourceCategory::Shading,
             PDFContentResourceCategory::Properties };
}

PDFObject PDFContentResources::write(PDFDocumentBuilder* builder) const
{
    // Everything needed was kept by read: the result is made of values only and the builder
    // gets no object here (the dictionaries of the editor are direct)
    Q_UNUSED(builder);

    // A copy of the original dictionary: /ProcSet and every key, which is not a category, stay
    PDFDictionaryBuilder resourcesDictionary = m_originalResources.isDictionary() ? PDFDictionaryBuilder(*m_originalResources.getDictionary()) : PDFDictionaryBuilder();

    for (const PDFContentResourceCategory category : getCategories())
    {
        const CategoryData* data = getCategoryData(category);
        if (!data || data->entries.size() == data->originalCount)
        {
            // Nothing added: the category keeps its original value (a reference stays a reference)
            continue;
        }

        // A new dictionary of this container only: the original one (often an indirect object
        // shared with other pages) is copied with all its entries, never changed in place
        PDFDictionaryBuilder categoryDictionary = data->originalDictionary.isDictionary() ? PDFDictionaryBuilder(*data->originalDictionary.getDictionary()) : PDFDictionaryBuilder();
        for (size_t i = data->originalCount; i < data->entries.size(); ++i)
        {
            const Entry& entry = data->entries[i];
            categoryDictionary.addEntry(PDFInplaceOrMemoryString(entry.name), entry.object);
        }

        resourcesDictionary.setEntry(PDFInplaceOrMemoryString(getCategoryKey(category)), PDFObject::createDictionary(std::move(categoryDictionary)));
    }

    return PDFObject::createDictionary(std::move(resourcesDictionary));
}

}   // namespace pdf
