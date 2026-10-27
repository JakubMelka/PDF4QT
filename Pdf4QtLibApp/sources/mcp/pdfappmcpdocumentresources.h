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


#ifndef PDFAPPMCPDOCUMENTRESOURCES_H
#define PDFAPPMCPDOCUMENTRESOURCES_H

#include "pdfappglobal.h"

#include "pdfmcpcall.h"
#include "pdfmcpcatalog.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

class QTimer;

namespace pdf
{

class PDFAppDocument;
class PDFAppMcpHost;
class PDFModifiedDocument;

/// Kind of a document resource (plan of step 7, 4.2)
enum class PDFAppMcpResourceKind
{
    Invalid,
    State,              ///< pdf4qt://application/state
    Info,               ///< …/info
    Pages,              ///< …/pages
    Outline,            ///< …/outline
    Text,               ///< …/text
    PageText,           ///< …/pages/{page}/text
    PageImage,          ///< …/pages/{page}/image/{dpi}
    Annotations,        ///< …/annotations
    PageAnnotations,    ///< …/pages/{page}/annotations
    Form,               ///< …/form
    Attachment          ///< …/attachments/{index}
};

/// A parsed URI of a document resource
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpResourceUri
{
public:
    PDFAppMcpResourceKind kind = PDFAppMcpResourceKind::Invalid;
    QString documentId;
    QString page;       ///< Value of {page} (validated by the reader)
    QString dpi;        ///< Value of {dpi}
    QString index;      ///< Value of {index}

    bool isValid() const { return kind != PDFAppMcpResourceKind::Invalid; }
};

/// Document resources of the server (plan of step 7, 4.2; package B): the
/// listed resources of the open documents (info, outline, annotations) and the
/// state of the application, the templates of the other ones, resources/read
/// with the read permission and the approval as for the tools - the same
/// readers as the tools (PDFAppMcpDocumentReader, the text service, the
/// renderer). cacheScope private, ttlMs 0 (the dispatcher adds them).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentResources
{
public:
    PDFAppMcpDocumentResources() = delete;

    static QString getStateUri() { return QStringLiteral("pdf4qt://application/state"); }

    /// Prefix of the URIs of a document ("pdf4qt://documents/<id>/")
    static QString getDocumentPrefix(const QString& documentId);

    /// Most pages of the resource …/pages ("then use the tool")
    static constexpr int MAXIMUM_RESOURCE_PAGES = 2000;

    /// Most annotations of the resource …/annotations
    static constexpr int MAXIMUM_RESOURCE_ANNOTATIONS = 1000;

    /// The listed resources: the state and info / outline / annotations of every open document
    static std::vector<PDFMcpResourceDefinition> createListed(const PDFAppMcpHost* host);

    /// The templates of the document resources
    static std::vector<PDFMcpResourceTemplateDefinition> createTemplates();

    /// Parses a URI of a document resource (an invalid one for any other URI)
    static PDFAppMcpResourceUri parseUri(const QString& uri);

    /// resources/read of a document resource
    static void read(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call);

    /// The read after its approval
    static void performRead(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call);

    /// Result { "contents": [ { uri, mimeType, text } ] }
    static PDFMcpCallResult createTextResult(const QString& uri, const QString& mimeType, const QString& text);

    /// Result { "contents": [ { uri, mimeType, blob } ] }
    static PDFMcpCallResult createBlobResult(const QString& uri, const QString& mimeType, QByteArrayView data);

    /// A protocol error of a resource in the era of the call (the URI in its data)
    static PDFMcpCallResult createError(const PDFMcpCall& call, const QString& message);

    /// Text of a read refused by the permission Deny
    static QString getDeniedText();
};

/// Watches the open documents for the resources (plan 4.2): a document opened,
/// closed, activated or renamed gives a new catalog (resources/list_changed) and
/// updates the state; every new version of a document updates its resources
/// (notifyResourceUpdated of the prefix of the document - the server coalesces
/// the updates). A child of the host.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentWatcher : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppMcpDocumentWatcher(PDFAppMcpHost* host);
    virtual ~PDFAppMcpDocumentWatcher() override;

    /// The watcher of the host: created with the first catalog of the host (the
    /// listed resources), so the host needs no member of package B
    static PDFAppMcpDocumentWatcher* ensure(PDFAppMcpHost* host);

    /// The watcher of the host (nullptr = none)
    static PDFAppMcpDocumentWatcher* find(const PDFAppMcpHost* host);

    /// Number of the catalogs rebuilt by the watcher (tests)
    int getRebuildCount() const { return m_rebuildCount; }

    /// A change of the documents waits for the coalescing of the rebuild (tests)
    bool isRebuildPending() const;

private:
    void connectDocument(PDFAppDocument* document);
    void connectWindow();
    void onDocumentAdded(pdf::PDFAppDocument* document);
    void onDocumentRemoved(const QString& id);
    void onRegistryDocumentChanged(pdf::PDFAppDocument* document);
    void onDocumentContentChanged(const pdf::PDFModifiedDocument& modified);
    void onCurrentTabChanged(int index);
    void onRebuildTimer();
    void scheduleRebuild();
    void notifyUpdated(const QString& uri);

    QPointer<PDFAppMcpHost> m_host;
    QPointer<QObject> m_tabBar;
    QPointer<QTimer> m_rebuildTimer;
    QSet<QString> m_listedKeys;     ///< Ids and titles of the listed documents (rebuild only on a change)
    int m_rebuildCount = 0;
};

}   // namespace pdf

#endif // PDFAPPMCPDOCUMENTRESOURCES_H
