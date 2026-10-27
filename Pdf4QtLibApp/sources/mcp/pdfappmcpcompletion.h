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


#ifndef PDFAPPMCPCOMPLETION_H
#define PDFAPPMCPCOMPLETION_H

#include "pdfappglobal.h"

#include "pdfmcpcall.h"

#include <QObject>
#include <QHash>

#include <deque>
#include <memory>

namespace pdf
{

class PDFAppMcpHost;

/// Rate limit of completion/complete per assistant (a child of the host)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpCompletionLimiter : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppMcpCompletionLimiter(QObject* parent);
    virtual ~PDFAppMcpCompletionLimiter() override;

    /// The limiter of the host (created at the first use)
    static PDFAppMcpCompletionLimiter* get(PDFAppMcpHost* host);

    /// A request of the assistant at the time (ms): true = allowed, else
    /// \p retryAfter gets the milliseconds until the next one is allowed
    bool allow(const QString& assistantKey, qint64 now, qint64* retryAfter);

    /// Current time of the limits (tests inject it; <= 0 = the clock)
    void setCurrentTime(qint64 time) { m_currentTime = time; }
    qint64 getCurrentTime() const;

private:
    QHash<QString, std::deque<qint64>> m_requests;
    qint64 m_currentTime = 0;
};

/// completion/complete (plan of step 7, 4.6; package B): the arguments of the
/// prompts (document titles, page numbers and labels, criteria, colors, depth)
/// and the variables of the resource templates ({documentId}, {page}, {dpi},
/// {index}, {topic}). Ids, titles, labels and attachment indices of the
/// documents are given only when Read documents is Allow, or Ask with the
/// approval of the assistant - otherwise the result is empty (no completion
/// based disclosure). At most 100 values; 20 requests per second per
/// assistant (-31001 with data.retryAfterMs).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpCompletion
{
public:
    PDFAppMcpCompletion() = delete;

    /// Most values of a result
    static constexpr int MAXIMUM_VALUES = 100;

    static void complete(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call);

    /// Result { "completion": { "values": [...], "total": n, "hasMore": b } }
    static PDFMcpCallResult createResult(const QStringList& values, int total, bool hasMore);

    /// Values of a prompt argument (all candidates, before the prefix filter)
    static QStringList getPromptValues(PDFAppMcpHost* host, const PDFMcpCallRequest& request, const QString& promptName,
                                       const QString& argumentName, const QJsonObject& context);

    /// Values of a variable of a resource template
    static QStringList getTemplateValues(PDFAppMcpHost* host, const PDFMcpCallRequest& request, const QString& uriTemplate,
                                         const QString& variable, const QString& prefix, const QJsonObject& context);
};

}   // namespace pdf

#endif // PDFAPPMCPCOMPLETION_H
