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

#include "pdfappdocumentregistry.h"
#include "pdfappdocument.h"

namespace pdf
{

PDFAppDocumentRegistry::PDFAppDocumentRegistry(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppDocumentRegistry::~PDFAppDocumentRegistry()
{
    for (const Entry& entry : m_entries)
    {
        if (entry.document)
        {
            entry.document->disconnect(this);
        }
    }
}

void PDFAppDocumentRegistry::addDocument(PDFAppDocument* document)
{
    if (!document || findDocument(document->getId()))
    {
        return;
    }

    Entry entry;
    entry.id = document->getId();
    entry.document = document;
    entry.object = document;
    m_entries.push_back(entry);

    connect(document, &QObject::destroyed, this, &PDFAppDocumentRegistry::onDocumentDestroyed);
    connect(document, &PDFAppDocument::stateChanged, this, &PDFAppDocumentRegistry::onDocumentChanged);
    connect(document, &PDFAppDocument::modifiedChanged, this, &PDFAppDocumentRegistry::onDocumentChanged);
    connect(document, &PDFAppDocument::lockChanged, this, &PDFAppDocumentRegistry::onDocumentChanged);
    connect(document, &PDFAppDocument::fileChanged, this, &PDFAppDocumentRegistry::onDocumentChanged);

    Q_EMIT documentAdded(document);
}

void PDFAppDocumentRegistry::removeDocument(PDFAppDocument* document)
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->object == document)
        {
            const QString id = it->id;
            if (it->document)
            {
                it->document->disconnect(this);
            }
            m_entries.erase(it);
            Q_EMIT documentRemoved(id);
            return;
        }
    }
}

PDFAppDocument* PDFAppDocumentRegistry::findDocument(const QString& id) const
{
    if (id.isEmpty())
    {
        return nullptr;
    }

    for (const Entry& entry : m_entries)
    {
        if (entry.document && entry.id == id)
        {
            return entry.document;
        }
    }
    return nullptr;
}

PDFAppDocument* PDFAppDocumentRegistry::findByCanonicalPath(const QString& canonicalPath) const
{
    if (canonicalPath.isEmpty())
    {
        return nullptr;
    }

#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif

    for (const Entry& entry : m_entries)
    {
        if (entry.document && !entry.document->getFilePath().isEmpty() &&
            entry.document->getFilePath().compare(canonicalPath, sensitivity) == 0)
        {
            return entry.document;
        }
    }
    return nullptr;
}

std::vector<PDFAppDocument*> PDFAppDocumentRegistry::getDocuments() const
{
    std::vector<PDFAppDocument*> documents;
    for (const Entry& entry : m_entries)
    {
        if (entry.document)
        {
            documents.push_back(entry.document);
        }
    }
    return documents;
}

void PDFAppDocumentRegistry::onDocumentDestroyed(QObject* object)
{
    // Only the pointer is compared, the document is not whole any more
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (it->object == object)
        {
            const QString id = it->id;
            m_entries.erase(it);
            Q_EMIT documentRemoved(id);
            return;
        }
    }
}

void PDFAppDocumentRegistry::onDocumentChanged()
{
    // The documents do not identify themselves in their signals
    PDFAppDocument* document = qobject_cast<PDFAppDocument*>(sender());
    if (document)
    {
        Q_EMIT documentChanged(document);
    }
}

}   // namespace pdf
