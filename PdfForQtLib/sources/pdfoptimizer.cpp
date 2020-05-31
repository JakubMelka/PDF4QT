//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#include "pdfoptimizer.h"
#include "pdfvisitor.h"
#include "pdfexecutionpolicy.h"

namespace pdf
{

class PDFRemoveSimpleObjectsVisitor : public PDFAbstractVisitor
{
public:
    explicit PDFRemoveSimpleObjectsVisitor(const PDFObjectStorage* storage, std::atomic<PDFInteger>* counter) :
        m_storage(storage),
        m_counter(counter)
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
    const PDFObjectStorage* m_storage;
    std::atomic<PDFInteger>* m_counter;
    std::vector<PDFObject> m_objectStack;
};

void PDFRemoveSimpleObjectsVisitor::visitNull()
{
    m_objectStack.push_back(PDFObject::createNull());
}

void PDFRemoveSimpleObjectsVisitor::visitBool(bool value)
{
    m_objectStack.push_back(PDFObject::createBool(value));
}

void PDFRemoveSimpleObjectsVisitor::visitInt(PDFInteger value)
{
    m_objectStack.push_back(PDFObject::createInteger(value));
}

void PDFRemoveSimpleObjectsVisitor::visitReal(PDFReal value)
{
    m_objectStack.push_back(PDFObject::createReal(value));
}

void PDFRemoveSimpleObjectsVisitor::visitString(PDFStringRef string)
{
    m_objectStack.push_back(PDFObject::createString(string));
}

void PDFRemoveSimpleObjectsVisitor::visitName(PDFStringRef name)
{
    m_objectStack.push_back(PDFObject::createName(name));
}

void PDFRemoveSimpleObjectsVisitor::visitArray(const PDFArray* array)
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

void PDFRemoveSimpleObjectsVisitor::visitDictionary(const PDFDictionary* dictionary)
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

void PDFRemoveSimpleObjectsVisitor::visitStream(const PDFStream* stream)
{
    const PDFDictionary* dictionary = stream->getDictionary();

    visitDictionary(dictionary);
    PDFObject dictionaryObject = m_objectStack.back();
    m_objectStack.pop_back();
    m_objectStack.push_back(PDFObject::createStream(std::make_shared<PDFStream>(PDFDictionary(*dictionaryObject.getDictionary()), QByteArray(*stream->getContent()))));
}

void PDFRemoveSimpleObjectsVisitor::visitReference(const PDFObjectReference reference)
{
    PDFObject object = m_storage->getObjectByReference(reference);

    switch (object.getType())
    {
        case PDFObject::Type::Null:
        case PDFObject::Type::Bool:
        case PDFObject::Type::Int:
        case PDFObject::Type::Real:
        case PDFObject::Type::String:
        case PDFObject::Type::Name:
            ++*m_counter;
            m_objectStack.push_back(qMove(object));
            break;

        default:
            m_objectStack.push_back(PDFObject::createReference(reference));
            break;
    }
}

PDFObject PDFRemoveSimpleObjectsVisitor::getObject()
{
    Q_ASSERT(m_objectStack.size() == 1);
    return qMove(m_objectStack.back());
}

PDFOptimizer::PDFOptimizer(OptimizationFlags flags, QObject* parent) :
    QObject(parent),
    m_flags(flags)
{

}

void PDFOptimizer::optimize()
{
    // Jakub Melka: We divide optimization into stages, each
    // stage can consist from multiple passes.
    constexpr auto stages = { OptimizationFlags(DereferenceSimpleObjects),
                              OptimizationFlags(RemoveNullObjects),
                              OptimizationFlags(RemoveUnusedObjects | MergeIdenticalObjects),
                              OptimizationFlags(ShrinkObjectStorage),
                              OptimizationFlags(RecompressFlateStreams) };

    int stage = 1;

    emit optimizationStarted();
    for (OptimizationFlags flags : stages)
    {
        emit optimizationProgress(tr("Stage %1").arg(stage++));
        OptimizationFlags currentSteps = flags & m_flags;

        int passIndex = 1;

        bool pass = true;
        while (pass)
        {
            emit optimizationProgress(tr("Pass %1").arg(passIndex++));
            pass = false;

            if (currentSteps.testFlag(DereferenceSimpleObjects))
            {
                pass = performDereferenceSimpleObjects() || pass;
            }
            if (currentSteps.testFlag(RemoveNullObjects))
            {
                pass = performRemoveNullObjects() || pass;
            }
            if (currentSteps.testFlag(RemoveUnusedObjects))
            {
                pass = performRemoveUnusedObjects() || pass;
            }
            if (currentSteps.testFlag(MergeIdenticalObjects))
            {
                pass = performMergeIdenticalObjects() || pass;
            }
            if (currentSteps.testFlag(ShrinkObjectStorage))
            {
                pass = performShrinkObjectStorage() || pass;
            }
            if (currentSteps.testFlag(RecompressFlateStreams))
            {
                pass = performRecompressFlateStreams() || pass;
            }
        }
    }
    emit optimizationFinished();
}

PDFOptimizer::OptimizationFlags PDFOptimizer::getFlags() const
{
    return m_flags;
}

void PDFOptimizer::setFlags(OptimizationFlags flags)
{
    m_flags = flags;
}

bool PDFOptimizer::performDereferenceSimpleObjects()
{
    std::atomic<PDFInteger> counter = 0;

    PDFObjectStorage::PDFObjects& objects =  m_storage.getObjects();
    auto processEntry = [this, &counter](PDFObjectStorage::Entry& entry)
    {
        PDFRemoveSimpleObjectsVisitor visitor(&m_storage, &counter);
        entry.object.accept(&visitor);
        entry.object = visitor.getObject();
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, objects.begin(), objects.end(), processEntry);
    emit optimizationProgress(tr("Simple objects dereferenced and embedded: %1").arg(counter));

    return false;
}

bool PDFOptimizer::performRemoveNullObjects()
{
    return false;
}

bool PDFOptimizer::performRemoveUnusedObjects()
{
    return false;
}

bool PDFOptimizer::performMergeIdenticalObjects()
{
    return false;
}

bool PDFOptimizer::performShrinkObjectStorage()
{
    return false;
}

bool PDFOptimizer::performRecompressFlateStreams()
{
    return false;
}

}   // namespace pdf
