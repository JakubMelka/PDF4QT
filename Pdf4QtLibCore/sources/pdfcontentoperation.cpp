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
// Plan: GUI_overhaul/edit_tool/zadani_jadro.md 4.2 (operations of a container).

#include "pdfcontentoperation.h"

#include <QtGlobal>

#include <utility>

#include "pdfdbgheap.h"

namespace pdf
{

// Ownership: an operation owns its values (the bytes are an implicitly shared QByteArray, the
// operands are QVariant values of the lexical analyzer, the names are byte arrays); nothing of it
// points into the document or into the processor it was read by.

PDFContentOperation PDFContentOperation::createOriginal(PDFContentOperator contentOperator,
                                                        QByteArray rawBytes,
                                                        PDFContentByteRange range,
                                                        Operands operands,
                                                        std::vector<PDFContentResourceName> resourceNames,
                                                        Flags flags)
{
    PDFContentOperation operation;
    operation.m_operator = contentOperator;
    operation.m_rawBytes = std::move(rawBytes);
    operation.m_range = range;
    operation.m_operands = std::move(operands);
    operation.m_resourceNames = std::move(resourceNames);
    operation.m_flags = flags;
    return operation;
}

PDFContentOperation PDFContentOperation::createGenerated(PDFContentOperator contentOperator,
                                                         QByteArray canonicalBytes,
                                                         Operands operands,
                                                         std::vector<PDFContentResourceName> resourceNames)
{
    // An inline image and an unknown operator have no canonical text: they are only copied
    Q_ASSERT(contentOperator != PDFContentOperator::InlineImage && contentOperator != PDFContentOperator::Invalid);

    PDFContentOperation operation;
    operation.m_operator = contentOperator;
    operation.m_rawBytes = std::move(canonicalBytes);
    operation.m_operands = std::move(operands);
    operation.m_resourceNames = std::move(resourceNames);
    operation.m_flags = Generated;
    return operation;
}

PDFContentOperation PDFContentOperation::createCopy(const PDFContentOperation& source)
{
    PDFContentOperation operation = source;

    // The copy is at no place of the original content
    operation.m_range = PDFContentByteRange();
    operation.m_flags.setFlag(Generated, true);
    operation.m_flags.setFlag(Suppressed, false);
    return operation;
}

PDFContentOperation PDFContentOperation::withFlags(Flags flags) const
{
    PDFContentOperation operation = *this;
    operation.m_flags = flags;
    return operation;
}

struct PDFContentOperationList::Chunk
{
    std::vector<PDFContentOperation> operations;
};

/// Parts of PDFContentOperationList, which need no access to its members
class PDFContentOperationListHelper
{
public:
    PDFContentOperationListHelper() = delete;

    /// The operation returned for an index, which is not valid (the caller broke the contract;
    /// a reference to nothing must never be returned)
    static const PDFContentOperation& getInvalidOperation()
    {
        static const PDFContentOperation s_invalidOperation;
        return s_invalidOperation;
    }
};

size_t PDFContentOperationList::getCount() const
{
    return m_count;
}

const PDFContentOperation& PDFContentOperationList::getOperation(size_t index) const
{
    if (index >= m_count)
    {
        Q_ASSERT(false);
        return PDFContentOperationListHelper::getInvalidOperation();
    }

    return m_chunks[index / CHUNK_SIZE]->operations[index % CHUNK_SIZE];
}

size_t PDFContentOperationList::getOriginalCount() const
{
    return m_originalCount;
}

const QByteArray& PDFContentOperationList::getTail() const
{
    return m_tail;
}

QByteArray PDFContentOperationList::getOriginalContent() const
{
    qsizetype size = m_tail.size();
    for (size_t i = 0; i < m_originalCount; ++i)
    {
        size += getOperation(i).getRawBytes().size();
    }

    // The flags play no role: a suppressed operation still is a part of the original content
    QByteArray content;
    content.reserve(size);
    for (size_t i = 0; i < m_originalCount; ++i)
    {
        content.append(getOperation(i).getRawBytes());
    }
    content.append(m_tail);
    return content;
}

PDFContentOperationList::Chunk* PDFContentOperationList::detachChunk(size_t chunkIndex)
{
    Q_ASSERT(chunkIndex < m_chunks.size());

    ChunkPointer& chunk = m_chunks[chunkIndex];

    // A block with one owner belongs to this list alone: no other list and no other thread can
    // reach it, so it is changed in place. A shared block may be read by other threads at this
    // moment (a list inside of a container), so it is never touched - this list gets a copy.
    if (chunk.use_count() != 1)
    {
        const size_t firstIndex = chunkIndex * CHUNK_SIZE;
        const size_t chunkCount = qMin(m_count - firstIndex, CHUNK_SIZE);

        ChunkPointer copy = std::make_shared<Chunk>();
        copy->operations.assign(chunk->operations.cbegin(), chunk->operations.cbegin() + qMin(chunkCount, chunk->operations.size()));
        chunk = std::move(copy);
    }

    return chunk.get();
}

size_t PDFContentOperationList::append(PDFContentOperation operation)
{
    if (m_isOriginalFinished && !operation.getFlags().testFlag(PDFContentOperation::Generated))
    {
        // After the original operations only operations of the editor follow: the indices below
        // getOriginalCount are the only places of the original content
        PDFContentOperation::Flags flags = operation.getFlags();
        flags.setFlag(PDFContentOperation::Generated, true);
        operation = operation.withFlags(flags);
    }

    const size_t index = m_count;
    const size_t chunkIndex = index / CHUNK_SIZE;

    if (chunkIndex == m_chunks.size())
    {
        m_chunks.push_back(std::make_shared<Chunk>());
    }

    Chunk* chunk = detachChunk(chunkIndex);
    Q_ASSERT(chunk->operations.size() == index % CHUNK_SIZE);
    chunk->operations.push_back(std::move(operation));
    ++m_count;

    return index;
}

void PDFContentOperationList::setFlags(size_t index, PDFContentOperation::Flags flags)
{
    if (index >= m_count)
    {
        Q_ASSERT(false);
        return;
    }

    if (getOperation(index).getFlags() == flags)
    {
        // Nothing to change: the block stays shared
        return;
    }

    Chunk* chunk = detachChunk(index / CHUNK_SIZE);
    PDFContentOperation& operation = chunk->operations[index % CHUNK_SIZE];
    operation = operation.withFlags(flags);
}

void PDFContentOperationList::finishOriginal(QByteArray tail)
{
    if (m_isOriginalFinished)
    {
        // The original content ends once; operations appended after it are never original
        Q_ASSERT(false);
        return;
    }

    m_originalCount = m_count;
    m_tail = std::move(tail);
    m_isOriginalFinished = true;
}

}   // namespace pdf
