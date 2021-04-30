//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt. If not, see <https://www.gnu.org/licenses/>.


#include "pdfobjectutils.h"
#include "pdfvisitor.h"

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
    PDFObject object = PDFObject::createArray(std::make_shared<PDFArray>(qMove(objects)));
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

    m_objectStack.push_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(entries))));
}

void PDFReplaceReferencesVisitor::visitStream(const PDFStream* stream)
{
    // Replace references in the dictionary
    visitDictionary(stream->getDictionary());
    PDFObject dictionaryObject = m_objectStack.back();
    m_objectStack.pop_back();
    m_objectStack.push_back(PDFObject::createStream(std::make_shared<PDFStream>(PDFDictionary(*dictionaryObject.getDictionary()), QByteArray(*stream->getContent()))));
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

PDFObject PDFObjectUtils::replaceReferences(const PDFObject& object, const std::map<PDFObjectReference, PDFObjectReference>& referenceMapping)
{
    PDFReplaceReferencesVisitor replaceReferencesVisitor(referenceMapping);
    object.accept(&replaceReferencesVisitor);
    return replaceReferencesVisitor.getObject();
}

}   // namespace pdf
