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

#ifndef PDFAPPDOCUMENTREGISTRY_H
#define PDFAPPDOCUMENTREGISTRY_H

#include "pdfappglobal.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <map>
#include <vector>

namespace pdf
{

class PDFAppDocument;

/// Registry of the open documents of the process (owned by PDFAppContext): id ->
/// document. The window finds the tab of an already open file by its canonical
/// path, Recent and MCP (step 7) list the documents. A document registers itself,
/// when its tab creates it, and leaves the registry, when it is destroyed (the
/// registry holds QPointers and never calls a destroyed document).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentRegistry : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppDocumentRegistry(QObject* parent = nullptr);
    virtual ~PDFAppDocumentRegistry() override;

    /// Adds the document (ignored for nullptr or an id, which is registered)
    void addDocument(PDFAppDocument* document);

    /// Removes the document (nothing, if it is not registered)
    void removeDocument(PDFAppDocument* document);

    /// Document of the id, nullptr if there is none
    PDFAppDocument* findDocument(const QString& id) const;

    /// First document of the canonical path (QFileInfo::canonicalFilePath; the
    /// comparison is case insensitive on Windows), nullptr if there is none
    PDFAppDocument* findByCanonicalPath(const QString& canonicalPath) const;

    /// All documents in the order of their registration
    std::vector<PDFAppDocument*> getDocuments() const;

Q_SIGNALS:
    void documentAdded(pdf::PDFAppDocument* document);
    void documentRemoved(const QString& id);

    /// The state, the saved state, the lock or the file of the document has changed
    void documentChanged(pdf::PDFAppDocument* document);

private:
    void onDocumentDestroyed(QObject* object);
    void onDocumentChanged();

    struct Entry
    {
        QString id;
        QPointer<PDFAppDocument> document;
        QObject* object = nullptr;
    };

    std::vector<Entry> m_entries;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTREGISTRY_H
