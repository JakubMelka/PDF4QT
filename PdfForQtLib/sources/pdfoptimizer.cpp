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
#include "pdfobjectutils.h"
#include "pdfutils.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"

namespace pdf
{

class PDFUpdateObjectVisitor : public PDFAbstractVisitor
{
public:
    explicit inline PDFUpdateObjectVisitor(const PDFObjectStorage* storage) :
        m_storage(storage)
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

protected:
    const PDFObjectStorage* m_storage;
    std::vector<PDFObject> m_objectStack;
};

void PDFUpdateObjectVisitor::visitNull()
{
    m_objectStack.push_back(PDFObject::createNull());
}

void PDFUpdateObjectVisitor::visitBool(bool value)
{
    m_objectStack.push_back(PDFObject::createBool(value));
}

void PDFUpdateObjectVisitor::visitInt(PDFInteger value)
{
    m_objectStack.push_back(PDFObject::createInteger(value));
}

void PDFUpdateObjectVisitor::visitReal(PDFReal value)
{
    m_objectStack.push_back(PDFObject::createReal(value));
}

void PDFUpdateObjectVisitor::visitString(PDFStringRef string)
{
    m_objectStack.push_back(PDFObject::createString(string));
}

void PDFUpdateObjectVisitor::visitName(PDFStringRef name)
{
    m_objectStack.push_back(PDFObject::createName(name));
}

void PDFUpdateObjectVisitor::visitArray(const PDFArray* array)
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

void PDFUpdateObjectVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    Q_ASSERT(dictionary);

    std::vector<PDFDictionary::DictionaryEntry> entries;
    entries.reserve(dictionary->getCount());

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        dictionary->getValue(i).accept(this);
        Q_ASSERT(!m_objectStack.empty());
        entries.emplace_back(dictionary->getKey(i), m_objectStack.back());
        m_objectStack.pop_back();
    }

    m_objectStack.push_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(entries))));
}

void PDFUpdateObjectVisitor::visitStream(const PDFStream* stream)
{
    const PDFDictionary* dictionary = stream->getDictionary();

    visitDictionary(dictionary);

    Q_ASSERT(!m_objectStack.empty());
    PDFObject dictionaryObject = m_objectStack.back();
    m_objectStack.pop_back();

    PDFDictionary newDictionary(*dictionaryObject.getDictionary());
    m_objectStack.push_back(PDFObject::createStream(std::make_shared<PDFStream>(qMove(newDictionary), QByteArray(*stream->getContent()))));
}

void PDFUpdateObjectVisitor::visitReference(const PDFObjectReference reference)
{
    m_objectStack.push_back(PDFObject::createReference(reference));
}

PDFObject PDFUpdateObjectVisitor::getObject()
{
    Q_ASSERT(m_objectStack.size() == 1);
    return qMove(m_objectStack.back());
}

class PDFRemoveSimpleObjectsVisitor : public PDFUpdateObjectVisitor
{
public:
    explicit inline PDFRemoveSimpleObjectsVisitor(const PDFObjectStorage* storage, std::atomic<PDFInteger>* counter) :
        PDFUpdateObjectVisitor(storage),
        m_counter(counter)
    {

    }

    virtual void visitReference(const PDFObjectReference reference) override;

private:
    std::atomic<PDFInteger>* m_counter;
};

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

class PDFRemoveNullDictionaryEntriesVisitor : public PDFUpdateObjectVisitor
{
public:
    explicit PDFRemoveNullDictionaryEntriesVisitor(const PDFObjectStorage* storage, std::atomic<PDFInteger>* counter) :
        PDFUpdateObjectVisitor(storage),
        m_counter(counter)
    {

    }

    virtual void visitDictionary(const PDFDictionary* dictionary) override;

private:
    std::atomic<PDFInteger>* m_counter;
};

void PDFRemoveNullDictionaryEntriesVisitor::visitDictionary(const PDFDictionary* dictionary)
{
    Q_ASSERT(dictionary);

    std::vector<PDFDictionary::DictionaryEntry> entries;
    entries.reserve(dictionary->getCount());

    for (size_t i = 0, count = dictionary->getCount(); i < count; ++i)
    {
        dictionary->getValue(i).accept(this);
        Q_ASSERT(!m_objectStack.empty());
        if (!m_objectStack.back().isNull())
        {
            entries.emplace_back(dictionary->getKey(i), m_objectStack.back());
        }
        else
        {
            ++*m_counter;
        }
        m_objectStack.pop_back();
    }

    m_objectStack.push_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(qMove(entries))));
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

    PDFObjectStorage::PDFObjects objects =  m_storage.getObjects();
    auto processEntry = [this, &counter](PDFObjectStorage::Entry& entry)
    {
        PDFRemoveSimpleObjectsVisitor visitor(&m_storage, &counter);
        entry.object.accept(&visitor);
        entry.object = visitor.getObject();
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, objects.begin(), objects.end(), processEntry);
    m_storage.setObjects(qMove(objects));
    emit optimizationProgress(tr("Simple objects dereferenced and embedded: %1").arg(counter));

    return false;
}

bool PDFOptimizer::performRemoveNullObjects()
{
    std::atomic<PDFInteger> counter = 0;

    PDFObjectStorage::PDFObjects objects =  m_storage.getObjects();
    auto processEntry = [this, &counter](PDFObjectStorage::Entry& entry)
    {
        PDFRemoveNullDictionaryEntriesVisitor visitor(&m_storage, &counter);
        entry.object.accept(&visitor);
        entry.object = visitor.getObject();
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, objects.begin(), objects.end(), processEntry);
    m_storage.setObjects(qMove(objects));
    emit optimizationProgress(tr("Null objects entries from dictionaries removed: %1").arg(counter));

    return false;
}

bool PDFOptimizer::performRemoveUnusedObjects()
{
    std::atomic<PDFInteger> counter = 0;
    PDFObjectStorage::PDFObjects objects =  m_storage.getObjects();
    std::set<PDFObjectReference> references = PDFObjectUtils::getReferences({ m_storage.getTrailerDictionary() }, m_storage);

    PDFIntegerRange<size_t> range(0, objects.size());
    auto processEntry = [this, &counter, &objects, &references](size_t index)
    {
        PDFObjectStorage::Entry& entry = objects[index];
        PDFObjectReference reference(PDFInteger(index), entry.generation);
        if (!references.count(reference) && !entry.object.isNull())
        {
            entry.object = PDFObject();
            ++counter;
        }
    };

    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, range.begin(), range.end(), processEntry);
    m_storage.setObjects(qMove(objects));
    emit optimizationProgress(tr("Unused objects removed: %1").arg(counter));

    return counter > 0;
}

bool PDFOptimizer::performMergeIdenticalObjects()
{
    std::atomic<PDFInteger> counter = 0;
    std::map<PDFObjectReference, PDFObjectReference> replacementMap;
    PDFObjectStorage::PDFObjects objects =  m_storage.getObjects();

    // Find same objects
    QMutex mutex;
    PDFIntegerRange<size_t> range(0, objects.size());
    auto processEntry = [this, &counter, &objects, &mutex, &replacementMap](size_t index)
    {
        const PDFObjectStorage::Entry& entry = objects[index];
        for (size_t i = 0; i < index; ++i)
        {
            if (objects[i].object.isNull())
            {
                // Jakub Melka: we do not merge null objects, they are just removed
                continue;
            }

            if (objects[i].object == entry.object)
            {
                QMutexLocker lock(&mutex);
                PDFObjectReference oldReference = PDFObjectReference(PDFInteger(index), objects[index].generation);
                PDFObjectReference newReference = PDFObjectReference(PDFInteger(i), objects[i].generation);
                replacementMap[oldReference] = newReference;
                ++counter;
                break;
            }
        }
    };
    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Unknown, range.begin(), range.end(), processEntry);

    // Replace objects
    if (!replacementMap.empty())
    {
        for (size_t i = 0; i < objects.size(); ++i)
        {
            objects[i].object = PDFObjectUtils::replaceReferences(objects[i].object, replacementMap);
        }
        PDFObject trailerDictionary = PDFObjectUtils::replaceReferences(m_storage.getTrailerDictionary(), replacementMap);
        m_storage.setTrailerDictionary(trailerDictionary);
    }

    m_storage.setObjects(qMove(objects));
    emit optimizationProgress(tr("Identical objects merged: %1").arg(counter));

    return counter > 0;
}

bool PDFOptimizer::performShrinkObjectStorage()
{
    std::map<PDFObjectReference, PDFObjectReference> replacementMap;
    PDFObjectStorage::PDFObjects objects =  m_storage.getObjects();

    auto isFree = [](const PDFObjectStorage::Entry& entry)
    {
        return entry.object.isNull() && entry.generation < PDF_MAX_OBJECT_GENERATION;
    };
    auto isOccupied = [](const PDFObjectStorage::Entry& entry)
    {
        return !entry.object.isNull();
    };

    // Make list of free usable indices
    std::vector<size_t> freeIndices;
    freeIndices.reserve(objects.size() / 8);

    const size_t objectCount = objects.size();
    for (size_t sourceIndex = 1; sourceIndex < objectCount; ++sourceIndex)
    {
        if (isFree(objects[sourceIndex]))
        {
            freeIndices.push_back(sourceIndex);
        }
    }
    std::reverse(freeIndices.begin(), freeIndices.end());

    // Move objects to free entries
    for (size_t sourceIndex = objectCount - 1; sourceIndex > 0; --sourceIndex)
    {
        if (freeIndices.empty())
        {
            // Jakub Melka: We have run out of free indices
            break;
        }

        PDFObjectStorage::Entry& sourceEntry = objects[sourceIndex];
        if (isOccupied(sourceEntry))
        {
            size_t targetIndex = freeIndices.back();
            freeIndices.pop_back();

            if (targetIndex >= sourceIndex)
            {
                break;
            }

            PDFObjectStorage::Entry& targetEntry = objects[targetIndex];
            Q_ASSERT(isFree(targetEntry));

            ++targetEntry.generation;
            targetEntry.object = qMove(sourceEntry.object);
            sourceEntry.object = PDFObject();

            replacementMap[PDFObjectReference(PDFInteger(sourceIndex), sourceEntry.generation)] = PDFObjectReference(PDFInteger(targetIndex), targetEntry.generation);
        }
    }

    // Shrink objects array
    for (size_t sourceIndex = objectCount - 1; sourceIndex > 0; --sourceIndex)
    {
        if (isOccupied(objects[sourceIndex]))
        {
            objects.resize(sourceIndex + 1);
            break;
        }
    }

    // Update objects
    if (!replacementMap.empty())
    {
        for (size_t i = 0; i < objects.size(); ++i)
        {
            objects[i].object = PDFObjectUtils::replaceReferences(objects[i].object, replacementMap);
        }
        PDFObject trailerDictionary = PDFObjectUtils::replaceReferences(m_storage.getTrailerDictionary(), replacementMap);

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Size");
        factory << PDFInteger(objects.size());
        factory.endDictionaryItem();
        factory.endDictionary();

        trailerDictionary = PDFObjectManipulator::merge(trailerDictionary, factory.takeObject(), PDFObjectManipulator::NoFlag);
        m_storage.setTrailerDictionary(trailerDictionary);
    }

    const size_t newObjectCount = objects.size();
    m_storage.setObjects(qMove(objects));
    emit optimizationProgress(tr("Object list shrinked by: %1").arg(objectCount - newObjectCount));

    return false;
}

bool PDFOptimizer::performRecompressFlateStreams()
{
    return false;
}

}   // namespace pdf