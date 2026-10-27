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

#ifndef PDFAPPMCPDOCUMENTRESOLVER_H
#define PDFAPPMCPDOCUMENTRESOLVER_H

#include "pdfappglobal.h"

#include "pdfmcpcall.h"

#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentTab;
class PDFAppMainWindow;

/// Result of a resolution of documentId
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpResolvedDocument
{
public:
    enum class Status
    {
        Resolved,
        InvalidId,      ///< Not 26 characters of base32
        NotFound,       ///< No open document of the id
        NoDocument,     ///< documentId omitted, no document is open
        Ambiguous,      ///< documentId omitted, several documents are open and no document tab is active
        NotReady,       ///< The document is loading, needs its password or failed (getState of the document)
        InputRequested  ///< Ambiguous: the client was asked to choose (the call is finished by its input request)
    };

    Status status = Status::NotFound;
    QPointer<PDFAppDocument> document;      ///< Resolved (also NotReady)
    std::vector<QPointer<PDFAppDocument>> candidates;   ///< Ambiguous: the open documents (elicitation, fallback text)
    QString errorText;                      ///< Execution error for the model (empty for Resolved)

    bool isResolved() const { return status == Status::Resolved && document; }
};

/// Resolution of the documentId of a call (plan 3.0, 2.8): a given id must be
/// an open document of the process (the registry of the context); an omitted
/// id means the document of the active document tab, else the only open
/// document; several documents without an active document tab are Ambiguous -
/// package B asks the client by an elicitation, without the capability the
/// fallback text lists the candidates ("Pass documentId, one of: …"). GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentResolver
{
public:
    explicit PDFAppMcpDocumentResolver(PDFAppContext* context);

    /// Resolves the id; \p requireReady: a document, which is not ready, gives NotReady
    PDFAppMcpResolvedDocument resolve(const QString& documentId, bool requireReady = true) const;

    /// Resolves the documentId of a call with the elicitation of plan 2.8 (package
    /// B): an ambiguous request takes the document chosen in the client (the
    /// answer of a retried request), else it asks the client, when it supports
    /// elicitation - the call is finished by its input request and the status is
    /// InputRequested (the caller only returns) - else it gives the fallback text
    /// (Ambiguous). A declined question is Ambiguous with its text.
    PDFAppMcpResolvedDocument resolveForCall(const std::shared_ptr<PDFMcpCall>& call, const QString& documentId, bool requireReady = true) const;

    /// Asks the client to choose one of the \p candidates (the call is finished
    /// by its input request); false, when the client cannot be asked
    bool askForDocument(const std::shared_ptr<PDFMcpCall>& call, const std::vector<QPointer<PDFAppDocument>>& candidates, const QString& message) const;

    /// The window of the context (one window per process, O2): the active one,
    /// else the first visible one; nullptr without a window
    PDFAppMainWindow* getWindow() const;

    /// Document of the active tab of the window, nullptr when the active tab is
    /// not a document tab
    PDFAppDocument* getActiveDocument() const;

    /// Tab of the document in the window, nullptr = none
    PDFAppDocumentTab* findTab(const PDFAppDocument* document) const;

    /// Open documents in the order of the registry
    std::vector<PDFAppDocument*> getDocuments() const;

    /// The id has the form of a document id (26 lower case base32 characters)
    static bool isValidDocumentId(const QString& documentId);

    /// "Invoice.pdf · 2 pages · background tab" (an elicitation option, a fallback list)
    QString describe(const PDFAppDocument* document) const;

    /// Fallback text without elicitation: "Several documents are open. Pass
    /// documentId, one of: k3f9… (Invoice.pdf), …"
    QString createAmbiguousText(const std::vector<QPointer<PDFAppDocument>>& candidates) const;

private:
    QPointer<PDFAppContext> m_context;
};

}   // namespace pdf

#endif // PDFAPPMCPDOCUMENTRESOLVER_H
