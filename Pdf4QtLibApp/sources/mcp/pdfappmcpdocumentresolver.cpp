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

#include "pdfappmcpdocumentresolver.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpelicitation.h"

#include "pdfmcpjson.h"

#include <QApplication>

namespace pdf
{

PDFAppMcpDocumentResolver::PDFAppMcpDocumentResolver(PDFAppContext* context) :
    m_context(context)
{

}

bool PDFAppMcpDocumentResolver::isValidDocumentId(const QString& documentId)
{
    if (documentId.size() != 26)
    {
        return false;
    }

    for (const QChar c : documentId)
    {
        const char16_t unit = c.unicode();
        if (!((unit >= u'a' && unit <= u'z') || (unit >= u'2' && unit <= u'7')))
        {
            return false;
        }
    }
    return true;
}

PDFAppMainWindow* PDFAppMcpDocumentResolver::getWindow() const
{
    if (!m_context)
    {
        return nullptr;
    }

    PDFAppMainWindow* active = qobject_cast<PDFAppMainWindow*>(QApplication::activeWindow());
    if (active && active->getContext() == m_context)
    {
        return active;
    }

    PDFAppMainWindow* hidden = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(widget);
        if (window && window->getContext() == m_context)
        {
            if (window->isVisible())
            {
                return window;
            }
            hidden = hidden ? hidden : window;
        }
    }
    return hidden;
}

PDFAppDocument* PDFAppMcpDocumentResolver::getActiveDocument() const
{
    PDFAppMainWindow* window = getWindow();
    if (!window)
    {
        return nullptr;
    }

    PDFAppDocumentTab* tab = qobject_cast<PDFAppDocumentTab*>(window->getCurrentTab());
    return tab ? tab->getDocument() : nullptr;
}

PDFAppDocumentTab* PDFAppMcpDocumentResolver::findTab(const PDFAppDocument* document) const
{
    PDFAppMainWindow* window = getWindow();
    if (!window || !document)
    {
        return nullptr;
    }

    for (PDFAppDocumentTab* tab : window->getDocumentTabs())
    {
        if (tab && tab->getDocument() == document)
        {
            return tab;
        }
    }
    return nullptr;
}

std::vector<PDFAppDocument*> PDFAppMcpDocumentResolver::getDocuments() const
{
    if (!m_context || !m_context->getDocumentRegistry())
    {
        return std::vector<PDFAppDocument*>();
    }
    return m_context->getDocumentRegistry()->getDocuments();
}

PDFAppMcpResolvedDocument PDFAppMcpDocumentResolver::resolve(const QString& documentId, bool requireReady) const
{
    PDFAppMcpResolvedDocument result;

    if (!documentId.isEmpty())
    {
        if (!isValidDocumentId(documentId))
        {
            result.status = PDFAppMcpResolvedDocument::Status::InvalidId;
            result.errorText = QStringLiteral("documentId must be the 26-character id of an open document from application.get_state.");
            return result;
        }

        PDFAppDocument* document = (m_context && m_context->getDocumentRegistry()) ? m_context->getDocumentRegistry()->findDocument(documentId) : nullptr;
        if (!document)
        {
            result.status = PDFAppMcpResolvedDocument::Status::NotFound;
            result.errorText = QStringLiteral("No open document has the id %1 - it may have been closed. Call application.get_state for the open documents.").arg(documentId);
            return result;
        }
        result.document = document;
    }
    else
    {
        const std::vector<PDFAppDocument*> documents = getDocuments();
        if (documents.empty())
        {
            result.status = PDFAppMcpResolvedDocument::Status::NoDocument;
            result.errorText = QStringLiteral("No document is open in PDF4QT. Ask the user to open one, or open a file by files.open with its absolute path.");
            return result;
        }

        PDFAppDocument* active = getActiveDocument();
        if (active)
        {
            result.document = active;
        }
        else if (documents.size() == 1)
        {
            result.document = documents.front();
        }
        else
        {
            result.status = PDFAppMcpResolvedDocument::Status::Ambiguous;
            for (PDFAppDocument* document : documents)
            {
                result.candidates.push_back(document);
            }
            result.errorText = createAmbiguousText(result.candidates);
            return result;
        }
    }

    if (requireReady && !result.document->isReady())
    {
        result.status = PDFAppMcpResolvedDocument::Status::NotReady;
        switch (result.document->getState())
        {
            case PDFAppDocumentState::Loading:
                result.errorText = QStringLiteral("The document is still loading in PDF4QT. Try again in a moment.");
                break;
            case PDFAppDocumentState::PasswordRequired:
                result.errorText = QStringLiteral("The document needs a password. Ask the user to type it in PDF4QT - assistants never receive passwords.");
                break;
            case PDFAppDocumentState::Failed:
                result.errorText = QStringLiteral("The document could not be opened in PDF4QT.");
                break;
            case PDFAppDocumentState::Ready:
                break;
        }
        return result;
    }

    result.status = PDFAppMcpResolvedDocument::Status::Resolved;
    return result;
}

PDFAppMcpResolvedDocument PDFAppMcpDocumentResolver::resolveForCall(const std::shared_ptr<PDFMcpCall>& call, const QString& documentId, bool requireReady) const
{
    PDFAppMcpResolvedDocument result = resolve(documentId, requireReady);
    if (result.status != PDFAppMcpResolvedDocument::Status::Ambiguous || !call)
    {
        return result;
    }

    // The document chosen in the client in an earlier round of the call (plan 2.8)
    const PDFMcpCallRequest& request = call->getRequest();
    const PDFAppMcpElicitationAnswer answer = PDFAppMcpElicitation::getAnswer(request, PDFAppMcpElicitation::getDocumentKey(), QStringLiteral("documentId"));
    switch (answer.status)
    {
        case PDFAppMcpElicitationAnswer::Status::Accepted:
        {
            PDFAppMcpResolvedDocument chosen = resolve(answer.value.toString(), requireReady);
            if (chosen.status != PDFAppMcpResolvedDocument::Status::NotFound && chosen.status != PDFAppMcpResolvedDocument::Status::InvalidId)
            {
                return chosen;
            }

            // The chosen document was closed meanwhile: the question is asked again
            break;
        }

        case PDFAppMcpElicitationAnswer::Status::Declined:
        case PDFAppMcpElicitationAnswer::Status::Canceled:
            result.errorText = PDFAppMcpElicitation::getDeclinedText(QStringLiteral("a document"), result.errorText);
            return result;

        case PDFAppMcpElicitationAnswer::Status::None:
        case PDFAppMcpElicitationAnswer::Status::Invalid:
            break;
    }

    if (askForDocument(call, result.candidates, QStringLiteral("Several documents are open in PDF4QT. Which document should the assistant use?")))
    {
        result.status = PDFAppMcpResolvedDocument::Status::InputRequested;
        result.errorText.clear();
    }
    return result;
}

bool PDFAppMcpDocumentResolver::askForDocument(const std::shared_ptr<PDFMcpCall>& call, const std::vector<QPointer<PDFAppDocument>>& candidates, const QString& message) const
{
    if (!call || !PDFAppMcpElicitation::isSupported(call->getRequest()))
    {
        return false;
    }

    std::vector<PDFAppMcpElicitationOption> options;
    for (const QPointer<PDFAppDocument>& document : candidates)
    {
        if (document && int(options.size()) < PDFAppMcpElicitation::MAXIMUM_OPTIONS)
        {
            PDFAppMcpElicitationOption option;
            option.value = document->getId();
            option.title = describe(document);
            options.push_back(std::move(option));
        }
    }

    // More documents than a question can offer: the fallback text lists them
    if (options.size() < 2 || candidates.size() > size_t(PDFAppMcpElicitation::MAXIMUM_OPTIONS))
    {
        return false;
    }

    const PDFMcpCallRequest& request = call->getRequest();
    PDFMcpInputRequests requests = PDFAppMcpElicitation::createRequests(request);
    PDFAppMcpElicitation::addChoice(request, requests, PDFAppMcpElicitation::getDocumentKey(), message, QStringLiteral("documentId"), QStringLiteral("Document"), options);
    call->requestInput(std::move(requests));
    return true;
}

QString PDFAppMcpDocumentResolver::describe(const PDFAppDocument* document) const
{
    if (!document)
    {
        return QString();
    }

    QStringList parts;
    parts << PDFMcpJson::sanitizeString(document->getTitle(), PDFMcpJson::SanitizeFlag::RemoveBidi, 256);
    if (document->isReady())
    {
        const PDFInteger pageCount = document->getPageCount();
        parts << (pageCount == 1 ? QStringLiteral("1 page") : QStringLiteral("%1 pages").arg(pageCount));
    }
    parts << (document == getActiveDocument() ? QStringLiteral("active tab") : QStringLiteral("background tab"));
    return parts.join(QStringLiteral(" · "));
}

QString PDFAppMcpDocumentResolver::createAmbiguousText(const std::vector<QPointer<PDFAppDocument>>& candidates) const
{
    QStringList items;
    for (const QPointer<PDFAppDocument>& document : candidates)
    {
        if (document && items.size() < 50)
        {
            items << QStringLiteral("%1 (%2)").arg(document->getId(), describe(document));
        }
    }
    return QStringLiteral("Several documents are open and no document tab is active. Pass documentId, one of: %1.").arg(items.join(QStringLiteral(", ")));
}

}   // namespace pdf
