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


#ifndef PDFAPPMCPREADSUPPORT_H
#define PDFAPPMCPREADSUPPORT_H

#include "pdfappglobal.h"
#include "pdfappmcpconfirmation.h"

#include "pdfglobal.h"
#include "pdfmcpcall.h"
#include "pdfmcprepository.h"

#include <QPointer>
#include <QPolygonF>
#include <QRectF>
#include <QString>

#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace pdf
{

class PDFAppDocument;
class PDFAppMcpHost;
class PDFDocument;
class PDFMcpJsonWriter;

/// What a read call does after its read approval (plan 2.7): the deferred part
/// of a resource read, a prompt or application.get_state, which reads the data
/// of the documents only after the user has allowed it
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDeferredRead
{
public:
    virtual ~PDFAppMcpDeferredRead();

    /// The read is allowed (GUI thread, from the event loop)
    virtual void proceed(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call) = 0;

    /// The read was not allowed (denied, timeout, unavailable, canceled)
    virtual void refuse(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, PDFAppMcpDecision decision) = 0;
};

/// Result of PDFAppMcpReadSupport::checkRead
enum class PDFAppMcpReadAccess
{
    Allowed,    ///< Read now
    Denied,     ///< Read documents is Deny - the caller answers its error
    Asked       ///< The read approval is shown; the continuation goes on later
};

/// Result of the resolution of a page given by page or by pageLabel
enum class PDFAppMcpPageResolution
{
    Resolved,
    Error,          ///< The error text is set
    InputRequested  ///< The client was asked to choose one of the pages of the label
};

/// Shared parts of the read tools, the document resources and the prompts of
/// package B (plan of step 7, chapter 3): the JSON of the results (rectangles,
/// quadrilaterals and numbers in the canonical form of the contract), the
/// structured result with its text block, the read approval of the requests,
/// which the host does not gate itself (resources, prompts, get_state), and
/// the pages given by numbers, ranges or labels (with the elicitation of an
/// ambiguous label).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpReadSupport
{
public:
    PDFAppMcpReadSupport() = delete;

    /// Most characters of a short text of a document in a result (a title, an author)
    static constexpr qsizetype MAXIMUM_NAME_LENGTH = 1024;

    /// Most characters of a long text of a document in a result (contents of an annotation)
    static constexpr qsizetype MAXIMUM_CONTENTS_LENGTH = 64 * 1024;

    // JSON --------------------------------------------------------------------

    /// documentId and title of the document (every document result, plan 3.0)
    static void writeDocumentHeader(PDFMcpJsonWriter& writer, const PDFAppDocument* document);

    /// A coordinate rounded to 1/1000 of a point (the outputs stay short)
    static double round(double value);

    /// { left, bottom, right, top } of a rectangle of the page space (the QRectF
    /// of Core has its y axis up: top() is the bottom edge)
    static void writeRect(PDFMcpJsonWriter& writer, QLatin1StringView key, const QRectF& rect);

    /// Arrays of 8 numbers of the quadrilaterals (4 points each, QuadPoints order)
    static void writeQuads(PDFMcpJsonWriter& writer, QLatin1StringView key, const QPolygonF& quadrilaterals);

    /// Text of the document in a result: lone surrogates and control characters
    /// removed (tab / newline kept), at most \p maximumLength characters
    static QString sanitizeContent(const QString& text, qsizetype maximumLength = MAXIMUM_CONTENTS_LENGTH);

    /// A short text of the document in a single line (bidirectional controls removed)
    static QString sanitizeName(const QString& text, qsizetype maximumLength = MAXIMUM_NAME_LENGTH);

    /// ISO 8601 of a date (an empty text for an invalid one)
    static QString formatDateTime(const QDateTime& dateTime);

    /// { "action": name, name: object }
    static QByteArray createStructuredContent(const QString& actionName, const QByteArray& actionObject);

    /// The tool result of a structured content: the text block is the compact
    /// JSON (or \p readableText), with the line of untrusted content first when
    /// \p hasDocumentContent (plan 3.0, 7)
    static PDFMcpCallResult createResult(const QString& actionName, const QByteArray& actionObject, bool hasDocumentContent, const QString& readableText = QString());

    // Read approval -----------------------------------------------------------

    /// The read approval of a request, which reads document data and which the
    /// host does not gate itself: Read = Deny gives Denied, Read = Ask without the
    /// approval of the assistant (and with an open document) shows the approval
    /// and the continuation proceeds or refuses later, else Allowed
    static PDFAppMcpReadAccess checkRead(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, const QString& actionId,
                                         PDFAppDocument* document, std::shared_ptr<PDFAppMcpDeferredRead> continuation);

    /// Read is allowed for the assistant without a question (Allow, or Ask with
    /// the approval, or no document is open)
    static bool isReadAllowed(const PDFAppMcpHost* host, const QString& assistantKey);

    /// Read is not denied for the assistant (Allow or Ask - with or without the approval)
    static bool isReadAvailable(const PDFAppMcpHost* host, const QString& assistantKey);

    /// Status of the activity row of a finished read: canceled, failed or ok
    static PDFMcpActivityStatus getActivityStatus(const PDFMcpCall& call, bool isError);

    /// Text of a read, which was not allowed
    static QString getReadRefusedText(PDFAppMcpDecision decision);

    // Pages -------------------------------------------------------------------

    /// Page indices of a range text ("1-3,7", empty = all), at most \p maximumPages
    static std::optional<std::vector<PDFInteger>> parsePages(const QString& text, PDFInteger pageCount, int maximumPages, QString* errorMessage);

    /// Page of a call given by page (from 1) or pageLabel (exactly one of them, or
    /// neither when \p defaultPageIndex >= 0): a label of several pages is asked
    /// in the client (plan 2.8) or gives the fallback text
    static PDFAppMcpPageResolution resolvePage(const std::shared_ptr<PDFMcpCall>& call, const PDFAppDocument* document,
                                               std::optional<qint64> page, const QString& pageLabel, PDFInteger defaultPageIndex,
                                               PDFInteger* pageIndex, QString* errorMessage);

    /// Activity pages (from 1) of page indices
    static std::set<int> getActivityPages(const std::vector<PDFInteger>& pageIndices);
};

}   // namespace pdf

#endif // PDFAPPMCPREADSUPPORT_H
