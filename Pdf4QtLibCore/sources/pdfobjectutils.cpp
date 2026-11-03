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

#include "pdfobjectutils.h"
#include "pdfdocument.h"
#include "pdfvisitor.h"
#include "pdfexecutionpolicy.h"
#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdfparser.h"
#include "pdfdbgheap.h"

namespace pdf
{

class PDFCollectReferencesVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFCollectReferencesVisitor(std::set<PDFObjectReference>& references) :
        m_references(references)
    {

    }

    virtual void visitArray(const PDFArray* array) override;
    virtual void visitDictionary(const PDFDictionary* dictionary) override;
    virtual void visitStream(const PDFStream* stream) override;
    virtual void visitReference(const PDFObjectReference reference) override;

private:
    std::set<PDFObjectReference>& m_references;
};

void PDFCollectReferencesVisitor::visitArray(const PDFArray* array)
{
    acceptArray(array);
}

void PDFCollectReferencesVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    acceptDictionary(dictionary);
}

void PDFCollectReferencesVisitor::visitStream(const PDFStream* stream)
{
    acceptStream(stream);
}

void PDFCollectReferencesVisitor::visitReference(const PDFObjectReference reference)
{
    m_references.insert(reference);
}

class PDFReplaceReferencesVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFReplaceReferencesVisitor(const std::map<PDFObjectReference, PDFObjectReference>& replacements) :
        m_replacements(replacements)
    {
        m_objectStack.reserve(32);
    }

    virtual void visitNull() override;
    virtual void visitBool(bool value) override;
    virtual void visitInt(PDFInteger value) override;
    virtual void visitReal(PDFReal value) override;
    virtual void visitString(PDFStringRef string) override;
    virtual void visitName(PDFStringRef name) override;
    virtual void visitArray(const PDFArray* array) override;
    virtual void visitDictionary(const PDFDictionary* dictionary) override;
    virtual void visitStream(const PDFStream* stream) override;
    virtual void visitReference(const PDFObjectReference reference) override;

    PDFObject getObject();

private:
    const std::map<PDFObjectReference, PDFObjectReference>& m_replacements;
    std::vector<PDFObject> m_objectStack;
};

void PDFReplaceReferencesVisitor::visitNull()
{
    m_objectStack.push_back(PDFObject::createNull());
}

void PDFReplaceReferencesVisitor::visitBool(bool value)
{
    m_objectStack.push_back(PDFObject::createBool(value));
}

void PDFReplaceReferencesVisitor::visitInt(PDFInteger value)
{
    m_objectStack.push_back(PDFObject::createInteger(value));
}

void PDFReplaceReferencesVisitor::visitReal(PDFReal value)
{
    m_objectStack.push_back(PDFObject::createReal(value));
}

void PDFReplaceReferencesVisitor::visitString(PDFStringRef string)
{
    m_objectStack.push_back(PDFObject::createString(string));
}

void PDFReplaceReferencesVisitor::visitName(PDFStringRef name)
{
    m_objectStack.push_back(PDFObject::createName(name));
}

void PDFReplaceReferencesVisitor::visitArray(const PDFArray* array)
{
    acceptArray(array);

    // We have all objects on the stack
    Q_ASSERT(array->getCount() <= m_objectStack.size());

    auto it = std::next(m_objectStack.cbegin(), m_objectStack.size() - array->getCount());
    std::vector<PDFObject> objects(it, m_objectStack.cend());
    PDFObject object = PDFObject::createArray(qMove(objects));
    m_objectStack.erase(it, m_objectStack.cend());
    m_objectStack.push_back(object);
}

void PDFReplaceReferencesVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    Q_ASSERT(dictionary);

    std::vector<PDFDictionary::DictionaryEntry> entries;
    entries.reserve(dictionary->getCount());

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        dictionary->getValue(i).accept(this);
        entries.emplace_back(dictionary->getKey(i), m_objectStack.back());
        m_objectStack.pop_back();
    }

    m_objectStack.push_back(PDFObject::createDictionary(qMove(entries)));
}

void PDFReplaceReferencesVisitor::visitStream(const PDFStream* stream)
{
    // Replace references in the dictionary
    visitDictionary(stream->getDictionary());
    PDFObject dictionaryObject = m_objectStack.back();
    m_objectStack.pop_back();
    m_objectStack.push_back(PDFObject::createStream(PDFStream(PDFDictionaryBuilder(*dictionaryObject.getDictionary()), QByteArray(*stream->getContent()))));
}

void PDFReplaceReferencesVisitor::visitReference(const PDFObjectReference reference)
{
    auto it = m_replacements.find(reference);
    if (it != m_replacements.cend())
    {
        // Replace the reference
        m_objectStack.push_back(PDFObject::createReference(it->second));
    }
    else
    {
        // Preserve old reference
        m_objectStack.push_back(PDFObject::createReference(reference));
    }
}

PDFObject PDFReplaceReferencesVisitor::getObject()
{
    Q_ASSERT(m_objectStack.size() == 1);
    return qMove(m_objectStack.back());
}

std::set<PDFObjectReference> PDFObjectUtils::getReferences(const std::vector<PDFObject>& objects, const PDFObjectStorage& storage)
{
    std::set<PDFObjectReference> references;
    {
        PDFCollectReferencesVisitor collectReferencesVisitor(references);
        for (const PDFObject& object : objects)
        {
            object.accept(&collectReferencesVisitor);
        }
    }

    // Iterative algorihm, which adds additional references from referenced objects.
    // If new reference is added, then we must also check, that all referenced objects
    // from this object are added.
    std::set<PDFObjectReference> workSet = references;
    while (!workSet.empty())
    {
        std::set<PDFObjectReference> addedReferences;
        PDFCollectReferencesVisitor collectReferencesVisitor(addedReferences);
        for (const PDFObjectReference& objectReference : workSet)
        {
            storage.getObject(objectReference).accept(&collectReferencesVisitor);
        }

        workSet.clear();
        std::set_difference(addedReferences.cbegin(), addedReferences.cend(), references.cbegin(), references.cend(), std::inserter(workSet, workSet.cend()));
        references.merge(addedReferences);
    }

    return references;
}

std::set<PDFObjectReference> PDFObjectUtils::getDirectReferences(const PDFObject& object)
{
    std::set<PDFObjectReference> references;

    PDFCollectReferencesVisitor collectReferencesVisitor(references);
    object.accept(&collectReferencesVisitor);

    return references;
}

bool PDFObjectUtils::isObjectStream(const PDFObject& object)
{
    if (!object.isStream())
    {
        return false;
    }

    const PDFDictionary* dictionary = object.getStream()->getDictionary();
    const PDFObject& type = dictionary->get("Type");
    return type.isName() && type.getString() == "ObjStm" && dictionary->get("N").isInt() && dictionary->get("First").isInt();
}

/// Recognition of the objects of the organization of a file (PDFObjectUtils::removeFileStructureObjects)
class PDFFileStructureObjectsHelper
{
public:
    PDFFileStructureObjectsHelper() = delete;

    /// PDF Reference 1.7, 3.4.7, Table 3.15: /Type /XRef with the required /Size and /W
    static bool isCrossReferenceStream(const PDFObject& object)
    {
        if (!object.isStream())
        {
            return false;
        }

        const PDFDictionary* dictionary = object.getStream()->getDictionary();
        const PDFObject& type = dictionary->get("Type");
        return type.isName() && type.getString() == "XRef" && dictionary->get("Size").isInt() && dictionary->get("W").isArray();
    }

    /// PDF Reference 1.7, Table F.1: the array /H of two or four integers
    static bool isHintArray(const PDFObject& object)
    {
        if (!object.isArray() || (object.getArray()->getCount() != 2 && object.getArray()->getCount() != 4))
        {
            return false;
        }

        for (size_t i = 0, count = object.getArray()->getCount(); i < count; ++i)
        {
            if (!object.getArray()->getItem(i).isInt())
            {
                return false;
            }
        }
        return true;
    }

    /// PDF Reference 1.7, Table F.1: the required entries of the linearization parameter dictionary
    static bool isLinearizationDictionary(const PDFObject& object)
    {
        if (!object.isDictionary())
        {
            return false;
        }

        const PDFDictionary* dictionary = object.getDictionary();
        const PDFObject& version = dictionary->get("Linearized");
        if (!version.isInt() && !version.isReal())
        {
            return false;
        }

        for (const char* key : { "L", "O", "E", "N", "T" })
        {
            if (!dictionary->get(key).isInt())
            {
                return false;
            }
        }
        return isHintArray(dictionary->get("H"));
    }

    /// PDF Reference 1.7, F.2.5: all entries of the dictionary are direct objects except of
    /// /Length. The primary hint stream has the required position /S of Table F.2, its other
    /// tables are positions (integers) too, the keys are open to new tables - so a name as
    /// /Type or /Subtype says it is another stream. The overflow hint stream has no such
    /// entries, its length is limited by the last number of /H ("including stream object
    /// overhead"), which is -1 without it.
    static bool isHintStream(const PDFObject& object, PDFInteger overflowLength)
    {
        if (!object.isStream())
        {
            return false;
        }

        const PDFStream* stream = object.getStream();
        const PDFDictionary* dictionary = stream->getDictionary();

        bool hasPosition = false;
        for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
        {
            const PDFInplaceOrMemoryString& key = dictionary->getKey(i);
            const PDFObject& value = dictionary->getValue(i);

            if (key == "Length")
            {
                if (!value.isInt() && !value.isReference())
                {
                    return false;
                }
            }
            else if (key == "Filter" || key == "DecodeParms" || key == "DL")
            {
                if (!PDFObjectUtils::getDirectReferences(value).empty())
                {
                    return false;
                }
            }
            else if (value.isInt())
            {
                hasPosition = true;
            }
            else
            {
                return false;
            }
        }

        return dictionary->get("S").isInt() || (!hasPosition && PDFInteger(stream->getContent()->size()) <= overflowLength);
    }

    /// True, if a reference leads to an object, which the storage does not have
    static bool hasMissingObject(const std::set<PDFObjectReference>& references, const PDFObjectStorage::PDFObjects& objects)
    {
        for (const PDFObjectReference& reference : references)
        {
            if (reference.objectNumber < 0 || size_t(reference.objectNumber) >= objects.size() || objects[size_t(reference.objectNumber)].object.isNull())
            {
                return true;
            }
        }
        return false;
    }

    /// True, if the object stream lists an object, which the storage does not have, so the
    /// stream may be its only copy (the permissive reading of a damaged file does not unpack
    /// object streams). A stream, which cannot be read, may hold anything. The reading has
    /// the limits of the reader (PDFDocumentReader::processObjectStreams).
    static bool isHoldingMissingObject(const PDFObjectStorage& storage, const PDFObject& object)
    {
        const PDFObjectStorage::PDFObjects& objects = storage.getObjects();
        const PDFStream* stream = object.getStream();

        try
        {
            const QByteArray data = storage.getDecodedStream(stream);
            const PDFInteger count = stream->getDictionary()->get("N").getInteger();
            const PDFInteger first = stream->getDictionary()->get("First").getInteger();
            if (first < 0 || first > data.size() || count < 0 || count > first / 3)
            {
                return true;
            }

            PDFParsingContext context([](PDFParsingContext*, PDFObjectReference) { return PDFObject(); });
            PDFParser parser(data, &context, PDFParser::None);
            for (PDFInteger i = 0; i < count; ++i)
            {
                const PDFObject number = parser.getObject();
                const PDFObject offset = parser.getObject();
                if (!number.isInt() || !offset.isInt())
                {
                    return true;
                }

                const PDFInteger objectNumber = number.getInteger();
                if (objectNumber <= 0 || size_t(objectNumber) >= objects.size() || objects[size_t(objectNumber)].object.isNull())
                {
                    return true;
                }
            }
        }
        catch (const PDFException&)
        {
            return true;
        }
        return false;
    }

    /// A container, which stays, because \p references lead to it, is no more removed
    /// and is added to the list of the containers, whose references count as well
    static void markReferences(std::set<PDFObjectReference>& references, std::vector<bool>& isRemoved, std::vector<size_t>& keptContainers)
    {
        for (const PDFObjectReference& reference : references)
        {
            if (reference.objectNumber >= 0 && size_t(reference.objectNumber) < isRemoved.size() && isRemoved[size_t(reference.objectNumber)])
            {
                isRemoved[size_t(reference.objectNumber)] = false;
                keptContainers.push_back(size_t(reference.objectNumber));
            }
        }
        references.clear();
    }
};

qint64 PDFObjectUtils::removeFileStructureObjects(PDFObjectStorage& storage)
{
    using Helper = PDFFileStructureObjectsHelper;

    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();

    bool isLinearized = false;
    PDFInteger overflowLength = -1;
    for (const PDFObjectStorage::Entry& entry : objects)
    {
        if (Helper::isLinearizationDictionary(entry.object))
        {
            isLinearized = true;

            const PDFArray* hints = entry.object.getDictionary()->get("H").getArray();
            if (hints->getCount() == 4)
            {
                overflowLength = qMax(overflowLength, hints->getItem(3).getInteger());
            }
        }
    }

    std::vector<bool> isRemoved(objects.size(), false);
    qint64 removedCount = 0;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        const PDFObject& object = objects[i].object;
        if (isObjectStream(object) ||
            Helper::isCrossReferenceStream(object) ||
            Helper::isLinearizationDictionary(object) ||
            (isLinearized && Helper::isHintStream(object, overflowLength)))
        {
            isRemoved[i] = true;
            ++removedCount;
        }
    }

    if (removedCount == 0)
    {
        return 0;
    }

    // Everything, which stays, may reference a container: the trailer and any other object,
    // whether it is reachable or not. A container, which stays by that, is such an object too.
    std::set<PDFObjectReference> references;
    PDFCollectReferencesVisitor visitor(references);

    storage.getTrailerDictionary().accept(&visitor);
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (!isRemoved[i])
        {
            objects[i].object.accept(&visitor);
        }
    }

    std::vector<size_t> keptContainers;

    // Only the files, whose references lead nowhere, pay for the decoding of the object streams
    if (Helper::hasMissingObject(references, objects))
    {
        for (size_t i = 0; i < objects.size(); ++i)
        {
            if (isRemoved[i] && isObjectStream(objects[i].object) && Helper::isHoldingMissingObject(storage, objects[i].object))
            {
                isRemoved[i] = false;
                keptContainers.push_back(i);
            }
        }
    }

    Helper::markReferences(references, isRemoved, keptContainers);
    while (!keptContainers.empty())
    {
        const size_t index = keptContainers.back();
        keptContainers.pop_back();
        --removedCount;

        objects[index].object.accept(&visitor);
        Helper::markReferences(references, isRemoved, keptContainers);
    }

    if (removedCount == 0)
    {
        return 0;
    }

    PDFObjectStorage::PDFObjects remainingObjects = objects;
    for (size_t i = 0; i < remainingObjects.size(); ++i)
    {
        if (isRemoved[i])
        {
            remainingObjects[i].object = PDFObject();
        }
    }
    storage.setObjects(std::move(remainingObjects));
    return removedCount;
}

PDFObject PDFObjectUtils::replaceReferences(const PDFObject& object, const std::map<PDFObjectReference, PDFObjectReference>& referenceMapping)
{
    PDFReplaceReferencesVisitor replaceReferencesVisitor(referenceMapping);
    object.accept(&replaceReferencesVisitor);
    return replaceReferencesVisitor.getObject();
}

QString PDFObjectUtils::getObjectTypeName(PDFObject::Type type)
{
    switch (type)
    {
        case pdf::PDFObject::Type::Null:
            return PDFTranslationContext::tr("Null");

        case pdf::PDFObject::Type::Bool:
            return PDFTranslationContext::tr("Boolean");

        case pdf::PDFObject::Type::Int:
            return PDFTranslationContext::tr("Integer");

        case pdf::PDFObject::Type::Real:
            return PDFTranslationContext::tr("Real");

        case pdf::PDFObject::Type::String:
            return PDFTranslationContext::tr("String");

        case pdf::PDFObject::Type::Name:
            return PDFTranslationContext::tr("Name");

        case pdf::PDFObject::Type::Array:
            return PDFTranslationContext::tr("Array");

        case pdf::PDFObject::Type::Dictionary:
            return PDFTranslationContext::tr("Dictionary");

        case pdf::PDFObject::Type::Stream:
            return PDFTranslationContext::tr("Stream");

        case pdf::PDFObject::Type::Reference:
            return PDFTranslationContext::tr("Reference");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

void PDFObjectClassifier::classify(const PDFDocument* document)
{
    // Clear old classification, if it exist
    m_classification.clear();
    m_allTypesUsed = None;

    if (!document)
    {
        return;
    }

    PDFDocumentDataLoaderDecorator loader(document);
    const PDFObjectStorage& storage = document->getStorage();
    const PDFObjectStorage::PDFObjects& objects = storage.getObjects();

    m_classification.resize(objects.size(), Classification());
    for (size_t i = 0; i < objects.size(); ++i)
    {
        PDFObjectReference reference(i, objects[i].generation);
        m_classification[i].reference = reference;
    }

    // First, iterate trough pages of the document
    const PDFCatalog* catalog = document->getCatalog();
    const size_t pageCount = catalog->getPageCount();
    for (size_t i = 0; i < pageCount; ++i)
    {
        const PDFPage* page = catalog->getPage(i);

        if (!page)
        {
            continue;
        }

        // Handle page itself
        if (hasObject(page->getPageReference()))
        {
            mark(page->getPageReference(), Page);
        }

        // Handle annotations
        for (const PDFObjectReference& reference : page->getAnnotations())
        {
            if (hasObject(reference))
            {
                mark(reference, Annotation);
            }
        }

        // Handle contents
        PDFObject pageObject = document->getObjectByReference(page->getPageReference());
        Q_ASSERT(pageObject.isDictionary());

        const PDFDictionary* dictionary = pageObject.getDictionary();
        const PDFObject& contentsObject = dictionary->get("Contents");
        if (contentsObject.isReference())
        {
            mark(contentsObject.getReference(), ContentStream);
        }

        // Handle resources
        if (const PDFDictionary* resourcesDictionary = document->getDictionaryFromObject(dictionary->get("Resources")))
        {
            markDictionary(document, resourcesDictionary->get("ExtGState"), GraphicState);
            markDictionary(document, resourcesDictionary->get("ColorSpace"), ColorSpace);
            markDictionary(document, resourcesDictionary->get("Pattern"), Pattern);
            markDictionary(document, resourcesDictionary->get("Shading"), Shading);
            markDictionary(document, resourcesDictionary->get("Font"), Font);

            if (const PDFDictionary* xobjectDictionary = document->getDictionaryFromObject(resourcesDictionary->get("XObject")))
            {
                const size_t count = xobjectDictionary->getCount();
                for (size_t xObjectIndex = 0; xObjectIndex < count; ++xObjectIndex)
                {
                    const PDFObject& item = xobjectDictionary->getValue(xObjectIndex);
                    if (item.isReference() && hasObject(item.getReference()))
                    {
                        if (const PDFDictionary* xobjectItemDictionary = document->getDictionaryFromObject(item))
                        {
                            QByteArray subtype = loader.readNameFromDictionary(xobjectItemDictionary, "Subtype");

                            if (subtype == "Image")
                            {
                                mark(item.getReference(), Image);
                            }
                            else if (subtype == "Form")
                            {
                                mark(item.getReference(), Form);
                            }
                        }
                    }
                }
            }
        }
    }

    for (Classification& classification : m_classification)
    {
        if (const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(classification.reference)))
        {
            QByteArray typeName = loader.readNameFromDictionary(dictionary, "Type");
            if (typeName == "Action")
            {
                classification.types.setFlag(Action);
            }
        }
    }

    for (const Classification& classification : m_classification)
    {
        m_allTypesUsed |= classification.types;
    }
}

bool PDFObjectClassifier::hasObject(PDFObjectReference reference) const
{
    return reference.isValid() &&
           reference.objectNumber < PDFInteger(m_classification.size()) &&
            m_classification[reference.objectNumber].reference == reference;
}

std::vector<PDFObjectReference> PDFObjectClassifier::getObjectsByType(Type type) const
{
    std::vector<PDFObjectReference> result;

    for (const Classification& classification : m_classification)
    {
        if (classification.types.testFlag(type))
        {
            result.push_back(classification.reference);
        }
    }

    return result;
}

PDFObjectClassifier::Statistics PDFObjectClassifier::calculateStatistics(const PDFDocument* document) const
{
    Statistics result;

    // Jakub Melka: prepare statistics map
    result.statistics[None];

    for (uint i = 0; i < 32; ++i)
    {
        uint32_t mask = 1 << i;
        if (m_allTypesUsed & mask)
        {
            result.statistics[Type(mask)];
        }
    }

    auto processEntry = [document, &result](const Classification& entry)
    {
        const PDFObject& object = document->getObjectByReference(entry.reference);

        if (object.isNull())
        {
            return;
        }

        Type type = Type(uint32_t(entry.types));
        if (!result.statistics.count(type))
        {
            type = None;
        }

        Q_ASSERT(result.statistics.count(type));

        const qint64 objectSize = PDFDocumentWriter::getObjectSize(document, entry.reference);

        StatisticsItem& statisticsItem = result.statistics.at(type);
        statisticsItem.count.fetch_add(1);
        statisticsItem.bytes.fetch_add(objectSize);
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, m_classification.cbegin(), m_classification.cend(), processEntry);

    PDFStatisticsCollector collector;
    PDFApplyVisitor(*document, &collector);

    for (PDFObject::Type objectType : PDFObject::getTypes())
    {
        result.objectCountByType[size_t(objectType)] = collector.getObjectCount(objectType);
    }

    return result;
}

void PDFObjectClassifier::mark(PDFObjectReference reference, Type type)
{
    Q_ASSERT(hasObject(reference));
    m_classification[reference.objectNumber].types.setFlag(type, true);
}

void PDFObjectClassifier::markDictionary(const PDFDocument* document, PDFObject object, Type type)
{
    if (const PDFDictionary* dictionary = document->getDictionaryFromObject(object))
    {
        const size_t count = dictionary->getCount();
        for (size_t i = 0; i < count; ++i)
        {
            const PDFObject& item = dictionary->getValue(i);
            if (item.isReference() && hasObject(item.getReference()))
            {
                mark(item.getReference(), type);
            }
        }
    }
}

PDFDictionaryBuilder PDFObjectUtils::copyDictionary(const PDFObjectStorage* storage, const PDFObject& object)
{
    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        return PDFDictionaryBuilder(*dictionary);
    }

    return PDFDictionaryBuilder();
}

std::vector<std::pair<PDFInteger, PDFObject>> PDFObjectUtils::readNumberTree(const PDFObjectStorage* storage, const PDFObject& root, int maximumDepth)
{
    std::vector<std::pair<PDFInteger, PDFObject>> entries;
    readNumberTreeNode(storage, root, 0, maximumDepth, entries);
    return entries;
}

void PDFObjectUtils::readNumberTreeNode(const PDFObjectStorage* storage, const PDFObject& node, int depth, int maximumDepth, std::vector<std::pair<PDFInteger, PDFObject>>& entries)
{
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(node);
    if (!dictionary || depth > maximumDepth)
    {
        return;
    }

    const PDFObject& numbers = storage->getObject(dictionary->get("Nums"));
    if (numbers.isArray())
    {
        const PDFArray* array = numbers.getArray();
        for (size_t i = 0; i + 1 < array->getCount(); i += 2)
        {
            const PDFObject& key = storage->getObject(array->getItem(i));
            if (key.isInt())
            {
                entries.emplace_back(key.getInteger(), array->getItem(i + 1));
            }
        }
    }

    const PDFObject& kids = storage->getObject(dictionary->get("Kids"));
    if (kids.isArray())
    {
        for (size_t i = 0; i < kids.getArray()->getCount(); ++i)
        {
            readNumberTreeNode(storage, kids.getArray()->getItem(i), depth + 1, maximumDepth, entries);
        }
    }
}

}   // namespace pdf
