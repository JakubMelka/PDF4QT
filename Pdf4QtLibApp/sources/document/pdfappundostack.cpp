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

#include "pdfappundostack.h"

#include "pdfobject.h"

namespace pdf
{

/// Estimate of the memory of the objects of a document
class PDFAppUndoStackHelper
{
public:
    PDFAppUndoStackHelper() = delete;

    /// Size of a PDFObject itself
    static constexpr qint64 OBJECT_SIZE = 16;

    /// Nesting, which is measured (deeper objects count as one object)
    static constexpr int MAXIMUM_DEPTH = 32;

    /// The two objects share their content in the heap (or are the same value)
    static bool isShared(const PDFObject& object, const PDFObject& other)
    {
        if (object.getType() != other.getType())
        {
            return false;
        }

        switch (object.getType())
        {
            case PDFObject::Type::Dictionary:
                return object.getDictionary() == other.getDictionary();

            case PDFObject::Type::Array:
                return object.getArray() == other.getArray();

            case PDFObject::Type::Stream:
                return object.getStream() == other.getStream();

            case PDFObject::Type::String:
            case PDFObject::Type::Name:
            {
                const PDFStringRef first = object.getStringObject();
                const PDFStringRef second = other.getStringObject();
                if (first.memoryString || second.memoryString)
                {
                    return first.memoryString == second.memoryString;
                }
                return object == other;
            }

            default:
                // Values without a content in the heap (only the object itself)
                return object == other;
        }
    }

    /// Memory of the object and of its content
    static qint64 getMemory(const PDFObject& object, int depth)
    {
        qint64 memory = OBJECT_SIZE;
        if (depth >= MAXIMUM_DEPTH)
        {
            return memory;
        }

        switch (object.getType())
        {
            case PDFObject::Type::Dictionary:
                memory += getDictionaryMemory(object.getDictionary(), depth);
                break;

            case PDFObject::Type::Array:
            {
                const PDFArray* array = object.getArray();
                for (const PDFObject& item : *array)
                {
                    memory += getMemory(item, depth + 1);
                }
                break;
            }

            case PDFObject::Type::Stream:
            {
                const PDFStream* stream = object.getStream();
                memory += stream->getContent()->size();
                memory += getDictionaryMemory(stream->getDictionary(), depth);
                break;
            }

            case PDFObject::Type::String:
            case PDFObject::Type::Name:
            {
                const PDFStringRef string = object.getStringObject();
                if (string.memoryString)
                {
                    memory += string.memoryString->getString().size();
                }
                break;
            }

            default:
                break;
        }

        return memory;
    }

    /// Memory of the entries of a dictionary (keys and values)
    static qint64 getDictionaryMemory(const PDFDictionary* dictionary, int depth)
    {
        qint64 memory = 0;
        if (!dictionary)
        {
            return memory;
        }

        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            memory += OBJECT_SIZE + getMemory(dictionary->getValue(i), depth + 1);
        }
        return memory;
    }

    /// Memory of the object, which it does not share with \p other (the object of
    /// the other document at the same place, nullptr if there is none): the parts
    /// of a changed dictionary, array or stream, which are still shared (the
    /// content of a stream with a changed dictionary, the unchanged entries), are
    /// not counted
    static qint64 getChangedMemory(const PDFObject& object, const PDFObject* other, int depth)
    {
        if (other && isShared(object, *other))
        {
            return 0;
        }
        if (!other || depth >= MAXIMUM_DEPTH || object.getType() != other->getType())
        {
            return getMemory(object, depth);
        }

        switch (object.getType())
        {
            case PDFObject::Type::Dictionary:
                return OBJECT_SIZE + getChangedDictionaryMemory(object.getDictionary(), other->getDictionary(), depth);

            case PDFObject::Type::Array:
            {
                const PDFArray* array = object.getArray();
                const PDFArray* otherArray = other->getArray();
                qint64 memory = OBJECT_SIZE;
                for (size_t i = 0; i < array->getCount(); ++i)
                {
                    memory += getChangedMemory(array->getItem(i), otherArray && i < otherArray->getCount() ? &otherArray->getItem(i) : nullptr, depth + 1);
                }
                return memory;
            }

            case PDFObject::Type::Stream:
            {
                const PDFStream* stream = object.getStream();
                const PDFStream* otherStream = other->getStream();
                qint64 memory = OBJECT_SIZE;
                if (!otherStream || !stream->getContent()->isSharedWith(*otherStream->getContent()))
                {
                    memory += stream->getContent()->size();
                }
                memory += getChangedDictionaryMemory(stream->getDictionary(), otherStream ? otherStream->getDictionary() : nullptr, depth);
                return memory;
            }

            default:
                return getMemory(object, depth);
        }
    }

    /// Memory of the entries of a dictionary, which it does not share with the
    /// entries of the same keys of \p other
    static qint64 getChangedDictionaryMemory(const PDFDictionary* dictionary, const PDFDictionary* other, int depth)
    {
        if (!other)
        {
            return getDictionaryMemory(dictionary, depth);
        }

        qint64 memory = 0;
        if (!dictionary)
        {
            return memory;
        }

        for (size_t i = 0; i < dictionary->getCount(); ++i)
        {
            memory += OBJECT_SIZE + getChangedMemory(dictionary->getValue(i), &other->get(dictionary->getKey(i)), depth + 1);
        }
        return memory;
    }
};

PDFAppUndoStack::PDFAppUndoStack(PDFAppDocument* document) :
    m_document(document)
{

}

void PDFAppUndoStack::push(PDFAppUndoStep step)
{
    // The saved version in the redo branch is not reachable any more
    if (m_savedPosition > int(m_undoSteps.size()))
    {
        m_savedPosition = -1;
    }
    m_redoSteps.clear();

    if (m_limit <= 0 || step.document.isNull())
    {
        // No undo: the version before the change is not reachable
        if (m_savedPosition == int(m_undoSteps.size()))
        {
            m_savedPosition = -1;
        }
        return;
    }

    m_undoSteps.push_back(std::move(step));
    trim();
}

QString PDFAppUndoStack::getUndoDescription() const
{
    return m_undoSteps.empty() ? QString() : m_undoSteps.back().description;
}

QString PDFAppUndoStack::getRedoDescription() const
{
    return m_redoSteps.empty() ? QString() : m_redoSteps.back().description;
}

bool PDFAppUndoStack::undo()
{
    if (m_undoSteps.empty() || !m_document || !m_document->isReady() || m_document->isLocked() || m_document->isBlockedByOperation())
    {
        return false;
    }

    PDFAppUndoStep step = std::move(m_undoSteps.back());
    m_undoSteps.pop_back();

    PDFAppUndoStep redoStep;
    redoStep.document = m_document->getDocument();
    redoStep.flags = step.flags;
    redoStep.description = step.description;
    redoStep.source = step.source;
    redoStep.memory = estimateMemory(redoStep.document.data(), step.document.data());
    m_redoSteps.push_back(std::move(redoStep));

    // The document may be deleted by a slot of its signals: nothing is used afterwards
    m_document->restore(step.document, step.flags);
    return true;
}

bool PDFAppUndoStack::redo()
{
    if (m_redoSteps.empty() || !m_document || !m_document->isReady() || m_document->isLocked() || m_document->isBlockedByOperation())
    {
        return false;
    }

    PDFAppUndoStep step = std::move(m_redoSteps.back());
    m_redoSteps.pop_back();

    PDFAppUndoStep undoStep;
    undoStep.document = m_document->getDocument();
    undoStep.flags = step.flags;
    undoStep.description = step.description;
    undoStep.source = step.source;
    undoStep.memory = estimateMemory(undoStep.document.data(), step.document.data());
    m_undoSteps.push_back(std::move(undoStep));

    // The document may be deleted by a slot of its signals: nothing is used afterwards
    m_document->restore(step.document, step.flags);
    return true;
}

void PDFAppUndoStack::clear()
{
    m_undoSteps.clear();
    m_redoSteps.clear();
    m_savedPosition = 0;
}

void PDFAppUndoStack::setLimit(int limit)
{
    m_limit = qMax(0, limit);
    trim();
}

void PDFAppUndoStack::setMemoryLimit(qint64 memoryLimit)
{
    m_memoryLimit = qMax<qint64>(0, memoryLimit);
    trim();
}

qint64 PDFAppUndoStack::getMemoryUsage() const
{
    qint64 memory = 0;
    for (const PDFAppUndoStep& step : m_undoSteps)
    {
        memory += step.memory;
    }
    for (const PDFAppUndoStep& step : m_redoSteps)
    {
        memory += step.memory;
    }
    return memory;
}

void PDFAppUndoStack::setSavedPosition()
{
    m_savedPosition = int(m_undoSteps.size());
}

int PDFAppUndoStack::getUnsavedChangeCount() const
{
    if (m_savedPosition < 0)
    {
        return -1;
    }
    return qAbs(int(m_undoSteps.size()) - m_savedPosition);
}

void PDFAppUndoStack::trim()
{
    // The redo steps go first, the farthest one (at the front - the step undone
    // first is at the front, the next redo step is at the back)
    qint64 memory = getMemoryUsage();
    while (!m_redoSteps.empty() && memory > m_memoryLimit)
    {
        memory -= m_redoSteps.front().memory;
        m_redoSteps.pop_front();
        if (m_savedPosition > int(m_undoSteps.size() + m_redoSteps.size()))
        {
            m_savedPosition = -1;
        }
    }

    // Then the oldest undo steps; the last undo step is kept over the memory limit
    while (!m_undoSteps.empty() && (m_undoSteps.size() > size_t(m_limit) || (memory > m_memoryLimit && m_undoSteps.size() > 1)))
    {
        memory -= m_undoSteps.front().memory;
        m_undoSteps.pop_front();

        // The positions move by one step; the version before the dropped step is gone
        if (m_savedPosition >= 0)
        {
            --m_savedPosition;
        }
    }
}

qint64 PDFAppUndoStack::estimateMemory(const PDFDocument* document, const PDFDocument* other)
{
    if (!document)
    {
        return 0;
    }

    const PDFObjectStorage::PDFObjects& objects = document->getStorage().getObjects();
    const PDFObjectStorage::PDFObjects* otherObjects = other ? &other->getStorage().getObjects() : nullptr;

    qint64 memory = 0;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        const PDFObject& object = objects[i].object;
        const PDFObject* otherObject = otherObjects && i < otherObjects->size() ? &(*otherObjects)[i].object : nullptr;
        memory += PDFAppUndoStackHelper::getChangedMemory(object, otherObject, 0);
    }
    return memory;
}

}   // namespace pdf
