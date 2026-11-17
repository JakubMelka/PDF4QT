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
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdfoperationcontrol.h"
#include "pdfresourcewalk.h"

#include <algorithm>
#include <deque>

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

/// One classification of PDFObjectClassifier: the roots of the classes (each kind by its own walk, each walk
/// with one visited set, a depth limit and a budget, iterative), then the class of every object by one
/// breadth first walk from all roots in the priority of the classes
class PDFObjectClassifierBuilder
{
public:
    using Type = PDFObjectClassifier::Type;

    explicit PDFObjectClassifierBuilder(PDFObjectClassifier* classifier, const PDFDocument* document, const PDFOperationControl* control) :
        m_classifier(classifier),
        m_document(document),
        m_storage(&document->getStorage()),
        m_control(control),
        m_classes(PDFObjectClassifier::getClasses()),
        m_seeds(m_classes.size())
    {

    }

    /// Classifies the document; false = canceled
    bool build()
    {
        collectObjects();
        if (!collectPages() || !collectResources())
        {
            return false;
        }
        collectCatalogActions();
        if (!collectActions())
        {
            return false;
        }
        return propagate();
    }

private:
    /// Keys, which lead back to a parent (the field of a widget, the page of an annotation, a node of a
    /// tree): the class of an object is never given to its parent
    static bool isBackLink(const PDFInplaceOrMemoryString& key)
    {
        return key == "Parent" || key == "P";
    }

    bool isCanceled() const
    {
        return PDFOperationControl::isOperationCanceled(m_control);
    }

    void addLimit(const PDFObjectWalk& walk)
    {
        if (walk.isLimitReached())
        {
            m_classifier->m_isComplete = false;
        }
    }

    size_t getClassIndex(Type type) const
    {
        return size_t(std::distance(m_classes.cbegin(), std::find(m_classes.cbegin(), m_classes.cend(), type)));
    }

    /// The references directly in an object and in its direct values (not followed; the back links left out)
    template<typename Callback>
    void forEachReference(const PDFObject& object, Callback callback) const
    {
        std::vector<const PDFObject*> stack = { &object };
        while (!stack.empty())
        {
            const PDFObject* current = stack.back();
            stack.pop_back();

            switch (current->getType())
            {
                case PDFObject::Type::Reference:
                    callback(current->getReference());
                    break;

                case PDFObject::Type::Array:
                {
                    const PDFArray* array = current->getArray();
                    for (size_t i = 0; i < array->getCount(); ++i)
                    {
                        stack.push_back(&array->getItem(i));
                    }
                    break;
                }

                case PDFObject::Type::Dictionary:
                case PDFObject::Type::Stream:
                {
                    const PDFDictionary* dictionary = current->isStream() ? current->getStream()->getDictionary() : current->getDictionary();
                    for (size_t i = 0; i < dictionary->getCount(); ++i)
                    {
                        if (!isBackLink(dictionary->getKey(i)))
                        {
                            stack.push_back(&dictionary->getValue(i));
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }
    }

    /// An object used as \p type: a reference is a root of the class; the references in a direct value get
    /// the class (they are no roots - a direct color space array is not an indirect object)
    void addRoot(const PDFObject& object, Type type)
    {
        std::vector<PDFObjectReference>& seeds = m_seeds[getClassIndex(type)];
        if (object.isReference())
        {
            const PDFObjectReference reference = object.getReference();
            if (m_classifier->hasObject(reference))
            {
                m_classifier->mark(reference, type);
                seeds.push_back(reference);
            }
        }
        else
        {
            forEachReference(object, [&seeds](PDFObjectReference reference) { seeds.push_back(reference); });
        }
    }

    /// The values of a dictionary (/AA) as actions
    void addActionValues(const PDFObject& object)
    {
        if (const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(object))
        {
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                m_actions.push_back(dictionary->getValue(i));
            }
        }
    }

    bool isName(const PDFObject& object, const char* name) const
    {
        const PDFObject& dereferenced = m_storage->getObject(object);
        return dereferenced.isName() && dereferenced.getString() == name;
    }

    /// The file structure (B206) and the dictionaries of /Type /Action
    void collectObjects()
    {
        const PDFObjectStorage::PDFObjects& objects = m_storage->getObjects();
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const PDFObject& object = objects[i].object;
            const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : (object.isDictionary() ? object.getDictionary() : nullptr);
            if (!dictionary)
            {
                continue;
            }

            const PDFObject reference = PDFObject::createReference(PDFObjectReference(PDFInteger(i), objects[i].generation));
            const PDFObject& type = dictionary->get("Type");
            if (object.isStream() && (isName(type, "ObjStm") || isName(type, "XRef")))
            {
                addRoot(reference, PDFObjectClassifier::FileStructure);
            }
            else if (object.isDictionary() && isName(type, "Action"))
            {
                m_actions.push_back(reference);
            }
        }
    }

    /// The pages, their content streams, their annotations, the actions of both
    bool collectPages()
    {
        const PDFCatalog* catalog = m_document->getCatalog();
        const size_t pageCount = catalog->getPageCount();
        for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            if (isCanceled())
            {
                return false;
            }

            const PDFPage* page = catalog->getPage(pageIndex);
            if (!page)
            {
                continue;
            }

            m_pageIndices.push_back(PDFInteger(pageIndex));
            const PDFObject pageReference = PDFObject::createReference(page->getPageReference());
            addRoot(pageReference, PDFObjectClassifier::Page);

            if (const PDFDictionary* pageDictionary = m_storage->getDictionaryFromObject(pageReference))
            {
                // ISO 32000-2, Table 31: /Contents is a stream or an array of streams; B190: a reference to a
                // missing object is skipped (hasObject in addRoot)
                const PDFObject& contents = pageDictionary->get("Contents");
                const PDFObject& resolvedContents = m_storage->getObject(contents);
                if (resolvedContents.isStream())
                {
                    addRoot(contents, PDFObjectClassifier::ContentStream);
                }
                else if (resolvedContents.isArray())
                {
                    const PDFArray* array = resolvedContents.getArray();
                    for (size_t i = 0; i < array->getCount(); ++i)
                    {
                        if (array->getItem(i).isReference() && m_storage->getObject(array->getItem(i)).isStream())
                        {
                            addRoot(array->getItem(i), PDFObjectClassifier::ContentStream);
                        }
                    }
                }

                addActionValues(pageDictionary->get("AA"));
            }

            for (const PDFObjectReference annotationReference : page->getAnnotations())
            {
                const PDFObject annotation = PDFObject::createReference(annotationReference);
                addRoot(annotation, PDFObjectClassifier::Annotation);
                if (const PDFDictionary* annotationDictionary = m_storage->getDictionaryFromObject(annotation))
                {
                    if (annotationDictionary->hasKey("A"))
                    {
                        m_actions.push_back(annotationDictionary->get("A"));
                    }
                    addActionValues(annotationDictionary->get("AA"));
                }
            }
        }

        return true;
    }

    /// The resources of every owner of resources (PDFResourceWalk) by their category (7.8.3, Table 34)
    bool collectResources()
    {
        const PDFResourceWalkResult walk = PDFResourceWalk::walk(m_document, m_pageIndices, PDFResourceWalk::Options(), m_control);
        if (walk.isCanceled)
        {
            return false;
        }
        if (walk.isLimitReached)
        {
            m_classifier->m_isComplete = false;
        }

        for (const PDFResourceWalkNode& node : walk.nodes)
        {
            const PDFObject owner = node.reference.isValid() ? PDFObject::createReference(node.reference) : PDFObject();
            switch (node.kind)
            {
                case PDFResourceOwnerKind::Form:
                case PDFResourceOwnerKind::AppearanceStream:
                case PDFResourceOwnerKind::SoftMaskGroup:
                    addRoot(owner, PDFObjectClassifier::Form);
                    break;

                case PDFResourceOwnerKind::Pattern:
                    addRoot(owner, PDFObjectClassifier::Pattern);
                    break;

                case PDFResourceOwnerKind::Type3Font:
                    addRoot(owner, PDFObjectClassifier::Font);
                    break;

                case PDFResourceOwnerKind::Page:
                    break;
            }

            // A resource dictionary shared by many owners is read once (review RS7CORE: N owners of one
            // dictionary of M resources made N * M roots)
            const PDFDictionary* resources = m_storage->getDictionaryFromObject(node.resources);
            if (!resources || !m_readDictionaries.insert(std::make_pair(resources, PDFObjectClassifier::None)).second)
            {
                continue;
            }

            addResources(resources->get("ExtGState"), PDFObjectClassifier::GraphicState);
            addResources(resources->get("ColorSpace"), PDFObjectClassifier::ColorSpace);
            addResources(resources->get("Pattern"), PDFObjectClassifier::Pattern);
            addResources(resources->get("Shading"), PDFObjectClassifier::Shading);
            addResources(resources->get("Font"), PDFObjectClassifier::Font);

            // XObjects by their /Subtype (8.8, Table 87): a PostScript XObject has no class
            const PDFDictionary* xObjects = m_storage->getDictionaryFromObject(resources->get("XObject"));
            if (xObjects && m_readDictionaries.insert(std::make_pair(xObjects, PDFObjectClassifier::Image)).second)
            {
                for (size_t i = 0; i < xObjects->getCount(); ++i)
                {
                    const PDFObject& value = xObjects->getValue(i);
                    const PDFObject& xObject = m_storage->getObject(value);
                    if (!xObject.isStream())
                    {
                        continue;
                    }

                    const PDFObject& subtype = xObject.getStream()->getDictionary()->get("Subtype");
                    if (isName(subtype, "Image"))
                    {
                        addRoot(value, PDFObjectClassifier::Image);
                    }
                    else if (isName(subtype, "Form"))
                    {
                        addRoot(value, PDFObjectClassifier::Form);
                    }
                }
            }
        }

        return true;
    }

    void addResources(const PDFObject& category, Type type)
    {
        // A category dictionary shared by more resource dictionaries is read once
        const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(category);
        if (dictionary && m_readDictionaries.insert(std::make_pair(dictionary, type)).second)
        {
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                addRoot(dictionary->getValue(i), type);
            }
        }
    }

    /// The actions of the catalog (12.6.2, 12.6.3, Table 197), of the outline (12.3.3, Table 151), of the
    /// fields (12.7.4, Table 228) and of the JavaScript name tree (7.7.4, Table 32)
    void collectCatalogActions()
    {
        const PDFDictionary* trailer = m_storage->getDictionaryFromObject(m_storage->getTrailerDictionary());
        const PDFDictionary* catalog = trailer ? m_storage->getDictionaryFromObject(trailer->get("Root")) : nullptr;
        if (!catalog)
        {
            return;
        }

        // /OpenAction is an array (a destination) or a dictionary (an action)
        const PDFObject& openAction = catalog->get("OpenAction");
        if (m_storage->getObject(openAction).isDictionary())
        {
            m_actions.push_back(openAction);
        }
        addActionValues(catalog->get("AA"));

        // The outline: the siblings at the depth of their parent, so a long list of items is no deep path
        {
            PDFObjectWalk walk;
            std::deque<std::pair<PDFObject, int>> queue;
            if (const PDFDictionary* outlines = m_storage->getDictionaryFromObject(catalog->get("Outlines")))
            {
                queue.emplace_back(outlines->get("First"), 0);
            }
            while (!queue.empty() && !isCanceled())
            {
                const auto [item, depth] = queue.front();
                queue.pop_front();
                if (item.isNull() || !walk.enter(item, depth))
                {
                    continue;
                }

                if (const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(item))
                {
                    if (dictionary->hasKey("A"))
                    {
                        m_actions.push_back(dictionary->get("A"));
                    }
                    queue.emplace_back(dictionary->get("First"), depth + 1);
                    queue.emplace_back(dictionary->get("Next"), depth);
                }
            }
            addLimit(walk);
        }

        // The fields and their kids
        {
            PDFObjectWalk walk;
            std::deque<std::pair<PDFObject, int>> queue;
            if (const PDFDictionary* form = m_storage->getDictionaryFromObject(catalog->get("AcroForm")))
            {
                addKids(form->get("Fields"), 0, &queue);
            }
            while (!queue.empty() && !isCanceled())
            {
                const auto [field, depth] = queue.front();
                queue.pop_front();
                if (!walk.enter(field, depth))
                {
                    continue;
                }

                if (const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(field))
                {
                    addActionValues(dictionary->get("AA"));
                    addKids(dictionary->get("Kids"), depth + 1, &queue);
                }
            }
            addLimit(walk);
        }

        // The JavaScript name tree: the values of /Names, the nodes of /Kids
        {
            PDFObjectWalk walk;
            std::deque<std::pair<PDFObject, int>> queue;
            if (const PDFDictionary* names = m_storage->getDictionaryFromObject(catalog->get("Names")))
            {
                queue.emplace_back(names->get("JavaScript"), 0);
            }
            while (!queue.empty() && !isCanceled())
            {
                const auto [node, depth] = queue.front();
                queue.pop_front();
                if (node.isNull() || !walk.enter(node, depth))
                {
                    continue;
                }

                if (const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(node))
                {
                    const PDFObject& namesArray = m_storage->getObject(dictionary->get("Names"));
                    if (namesArray.isArray())
                    {
                        const PDFArray* array = namesArray.getArray();
                        for (size_t i = 1; i < array->getCount(); i += 2)
                        {
                            m_actions.push_back(array->getItem(i));
                        }
                    }
                    addKids(dictionary->get("Kids"), depth + 1, &queue);
                }
            }
            addLimit(walk);
        }
    }

    void addKids(const PDFObject& kids, int depth, std::deque<std::pair<PDFObject, int>>* queue) const
    {
        const PDFObject& array = m_storage->getObject(kids);
        if (array.isArray())
        {
            for (size_t i = 0; i < array.getArray()->getCount(); ++i)
            {
                queue->emplace_back(array.getArray()->getItem(i), depth);
            }
        }
    }

    /// The actions found and their chains of /Next (12.6.2: a dictionary or an array of dictionaries)
    bool collectActions()
    {
        PDFObjectWalk walk;
        std::deque<std::pair<PDFObject, int>> queue;
        for (const PDFObject& action : m_actions)
        {
            queue.emplace_back(action, 0);
        }
        m_actions.clear();

        while (!queue.empty())
        {
            if (isCanceled())
            {
                return false;
            }

            const auto [action, depth] = queue.front();
            queue.pop_front();
            if (!m_storage->getObject(action).isDictionary() || !walk.enter(action, depth))
            {
                continue;
            }

            // The actions of /Next are the siblings of the action (they follow it), not its children: a long
            // chain is no deep path (the walk is iterative, its visited set ends a cycle)
            addRoot(action, PDFObjectClassifier::Action);
            const PDFDictionary* dictionary = m_storage->getDictionaryFromObject(action);
            const PDFObject& next = dictionary->get("Next");
            const PDFObject& resolvedNext = m_storage->getObject(next);
            if (resolvedNext.isArray())
            {
                addKids(next, depth, &queue);
            }
            else if (resolvedNext.isDictionary())
            {
                queue.emplace_back(next, depth);
            }
        }

        addLimit(walk);
        return true;
    }

    /// The class of every object: one breadth first walk from all roots (the roots of a class of a higher
    /// priority first), an object takes the class of the first walk which reaches it
    bool propagate()
    {
        PDFObjectWalk walk;
        std::deque<std::pair<PDFObjectReference, int>> queue;
        std::vector<PDFObjectClassifier::Classification>& entries = m_classifier->m_classification;

        for (size_t classIndex = 0; classIndex < m_classes.size(); ++classIndex)
        {
            for (const PDFObjectReference reference : m_seeds[classIndex])
            {
                if (m_classifier->hasObject(reference) && walk.enterReference(reference, 0))
                {
                    entries[size_t(reference.objectNumber)].assigned = m_classes[classIndex];
                    queue.emplace_back(reference, 0);
                }
            }
        }

        size_t steps = 0;
        while (!queue.empty())
        {
            if (++steps % 4096 == 0 && isCanceled())
            {
                return false;
            }

            const PDFObjectReference reference = queue.front().first;
            const int depth = queue.front().second;
            queue.pop_front();
            const Type type = entries[size_t(reference.objectNumber)].assigned;
            forEachReference(m_storage->getObject(reference), [&](PDFObjectReference child)
            {
                if (m_classifier->hasObject(child) && !walk.isVisited(child) && walk.enterReference(child, depth + 1))
                {
                    entries[size_t(child.objectNumber)].assigned = type;
                    queue.emplace_back(child, depth + 1);
                }
            });
        }

        addLimit(walk);
        return true;
    }

    PDFObjectClassifier* m_classifier;
    const PDFDocument* m_document;
    const PDFObjectStorage* m_storage;
    const PDFOperationControl* m_control;
    std::vector<Type> m_classes;
    std::vector<std::vector<PDFObjectReference>> m_seeds;   ///< The objects of each class (the index of m_classes), whose class is given
    std::vector<PDFObject> m_actions;                       ///< The actions found, before their chains are walked
    std::vector<PDFInteger> m_pageIndices;
    std::set<std::pair<const PDFDictionary*, Type>> m_readDictionaries;    ///< The resource dictionaries (None) and their categories (by the class) read already
};

void PDFObjectClassifier::classify(const PDFDocument* document)
{
    classify(document, nullptr);
}

bool PDFObjectClassifier::classify(const PDFDocument* document, const PDFOperationControl* control)
{
    // Clear old classification, if it exist
    m_classification.clear();
    m_allTypesUsed = None;
    m_isComplete = true;

    if (!document)
    {
        return true;
    }

    const PDFObjectStorage::PDFObjects& objects = document->getStorage().getObjects();
    m_classification.resize(objects.size(), Classification());
    for (size_t i = 0; i < objects.size(); ++i)
    {
        m_classification[i].reference = PDFObjectReference(PDFInteger(i), objects[i].generation);
    }

    PDFObjectClassifierBuilder builder(this, document, control);
    const bool isFinished = builder.build();
    if (!isFinished)
    {
        m_isComplete = false;
    }

    for (const Classification& classification : m_classification)
    {
        m_allTypesUsed |= classification.types;
    }

    return isFinished;
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

qint64 PDFObjectClassifier::getObjectCountByType(Type type) const
{
    return std::count_if(m_classification.cbegin(), m_classification.cend(), [type](const Classification& classification) { return classification.types.testFlag(type); });
}

PDFObjectClassifier::Type PDFObjectClassifier::getClass(PDFObjectReference reference) const
{
    return hasObject(reference) ? m_classification[size_t(reference.objectNumber)].assigned : None;
}

std::vector<PDFObjectClassifier::Type> PDFObjectClassifier::getClasses()
{
    // The file structure first: an object stream is never content; a page before what it uses
    return { FileStructure, Page, ContentStream, Annotation, Image, Form, Font, Pattern, Shading, ColorSpace, GraphicState, Action, None };
}

PDFObjectClassifier::Statistics PDFObjectClassifier::calculateStatistics(const PDFDocument* document) const
{
    return calculateStatistics(document, nullptr);
}

PDFObjectClassifier::Statistics PDFObjectClassifier::calculateStatistics(const PDFDocument* document, const PDFOperationControl* control) const
{
    Statistics result;

    // The rows of the classes, which have an object (the map is not changed by the parallel loop)
    result.statistics[None];
    for (const Classification& classification : m_classification)
    {
        result.statistics[classification.assigned];
    }

    if (!document)
    {
        return result;
    }

    std::array<std::atomic<qint64>, size_t(PDFObject::Type::LastType)> valueCounts = { };
    auto processEntry = [document, control, &result, &valueCounts](const Classification& entry)
    {
        const PDFObject& object = document->getObjectByReference(entry.reference);

        // A free entry of the storage is no object
        if (object.isNull() || PDFOperationControl::isOperationCanceled(control))
        {
            return;
        }

        const qint64 objectSize = PDFDocumentWriter::getObjectSize(document, entry.reference);

        StatisticsItem& statisticsItem = result.statistics.at(entry.assigned);
        statisticsItem.count.fetch_add(1);
        statisticsItem.bytes.fetch_add(objectSize);

        // The values of the object by their type (iterative: the stack never depends on the nesting)
        std::array<qint64, size_t(PDFObject::Type::LastType)> counts = { };
        std::vector<const PDFObject*> stack = { &object };
        while (!stack.empty())
        {
            const PDFObject* current = stack.back();
            stack.pop_back();
            ++counts[size_t(current->getType())];

            if (current->isArray())
            {
                const PDFArray* array = current->getArray();
                for (size_t i = 0; i < array->getCount(); ++i)
                {
                    stack.push_back(&array->getItem(i));
                }
            }
            else if (current->isDictionary() || current->isStream())
            {
                const PDFDictionary* dictionary = current->isStream() ? current->getStream()->getDictionary() : current->getDictionary();
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    stack.push_back(&dictionary->getValue(i));
                }
            }
        }

        for (size_t i = 0; i < counts.size(); ++i)
        {
            if (counts[i] > 0)
            {
                valueCounts[i].fetch_add(counts[i]);
            }
        }
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, m_classification.cbegin(), m_classification.cend(), processEntry);

    for (size_t i = 0; i < valueCounts.size(); ++i)
    {
        result.objectCountByType[i] = valueCounts[i].load();
    }
    result.isCanceled = PDFOperationControl::isOperationCanceled(control);
    return result;
}

void PDFObjectClassifier::mark(PDFObjectReference reference, Type type)
{
    // B190: a reference to a missing object (/Contents 99999 0 R) is never written
    if (hasObject(reference))
    {
        m_classification[size_t(reference.objectNumber)].types.setFlag(type, true);
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
    PDFObjectWalk walk(maximumDepth);
    readNumberTreeNode(storage, root, 0, &walk, entries);
    return entries;
}

void PDFObjectUtils::readNumberTreeNode(const PDFObjectStorage* storage, const PDFObject& node, int depth, PDFObjectWalk* walk, std::vector<std::pair<PDFInteger, PDFObject>>& entries)
{
    if (!walk->enter(node, depth))
    {
        return;
    }

    const PDFDictionary* dictionary = storage->getDictionaryFromObject(node);
    if (!dictionary)
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
            readNumberTreeNode(storage, kids.getArray()->getItem(i), depth + 1, walk, entries);
        }
    }
}

PDFObjectWalk::PDFObjectWalk(int maximumDepth, qint64 maximumNodes) :
    m_maximumDepth(maximumDepth),
    m_maximumNodes(maximumNodes)
{

}

bool PDFObjectWalk::enter(const PDFObject& node, int depth)
{
    return node.isReference() ? enterReference(node.getReference(), depth) : enterNode(depth);
}

bool PDFObjectWalk::enterReference(PDFObjectReference reference, int depth)
{
    if (!reference.isValid())
    {
        return enterNode(depth);
    }

    if (m_visited.count(reference))
    {
        return false;
    }

    if (!enterNode(depth))
    {
        // Not marked: a shorter path may still reach the object
        return false;
    }

    m_visited.insert(reference);
    return true;
}

bool PDFObjectWalk::enterNode(int depth)
{
    if (depth < 0 || depth > m_maximumDepth || m_nodeCount >= m_maximumNodes)
    {
        m_isLimitReached = true;
        return false;
    }

    ++m_nodeCount;
    return true;
}

}   // namespace pdf
