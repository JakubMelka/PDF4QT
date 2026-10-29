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

#include "pdfappmcpjob.h"
#include "pdfappmcphost.h"
#include "pdfthreadannotations.h"

#include "pdfexception.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"

#include <new>

namespace pdf
{

PDFAppMcpJob::PDFAppMcpJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot) :
    m_call(std::move(call)),
    m_snapshot(std::move(snapshot))
{

}

PDFAppMcpJob::~PDFAppMcpJob() = default;

void PDFAppMcpJob::finish(PDFAppMcpHost* host)
{
    Q_UNUSED(host);
}

PDFAppMcpJobRunnable::PDFAppMcpJobRunnable(std::shared_ptr<PDFAppMcpJob> job, PDFAppMcpHost* host) :
    m_job(std::move(job)),
    m_host(host)
{
    setAutoDelete(true);
}

PDFAppMcpJobRunnable::~PDFAppMcpJobRunnable() = default;

PDFMcpCallResult PDFAppMcpJobRunnable::createFailure(const PDFMcpCall& call, const QString& text)
{
    // A tool reports an execution error, the other requests a protocol error
    if (call.getRequest().kind == PDFMcpCallKind::ToolCall)
    {
        return PDFMcpCallResult::createToolError(text);
    }
    return PDFMcpCallResult::createError(PDFMcpErrorCodes::INTERNAL_ERROR, text);
}

void PDFAppMcpJobRunnable::execute(PDFAppMcpJob* job)
{
    const std::shared_ptr<PDFMcpCall>& call = job->getCall();

    // A call canceled before its job ran is not executed: the job finishes it
    // in the GUI thread (with its activity row canceled), and the host finishes
    // it, when the job has not (PDFAppMcpHost::onJobFinished)
    if (call && call->isCanceled())
    {
        return;
    }

    try
    {
        job->execute();
    }
    catch (const PDFException& exception)
    {
        // The message may quote the document (names, broken tokens): one bounded
        // line without control and bidirectional characters
        if (call)
        {
            const QString message = PDFMcpJson::sanitizeString(exception.getMessage(), PDFMcpJson::SanitizeFlag::RemoveBidi, MAXIMUM_MESSAGE_LENGTH);
            call->finish(createFailure(*call, QStringLiteral("PDF4QT could not process the document: %1").arg(message)));
        }
    }
    catch (const std::bad_alloc&)
    {
        if (call)
        {
            call->finish(createFailure(*call, QStringLiteral("PDF4QT ran out of memory. Ask for less data (fewer pages, a lower dpi).")));
        }
    }
    catch (...)
    {
        // Any other exception (of the standard library, of a third-party library)
        // ends the call by a generic failure - its text never reaches the client
        if (call)
        {
            call->finish(createFailure(*call, QStringLiteral("PDF4QT could not process the request because of an internal error.")));
        }
    }
}

void PDFAppMcpJobRunnable::run()
{
    // The queue of the thread pool synchronizes inside of Qt
    PDFThreadAnnotations::happensAfter(this);
    PDFThreadAnnotations::forget(this);

    if (!m_job)
    {
        return;
    }

    // The host bounds the heavy jobs before they reach the pool: a worker never
    // waits here for another job
    execute(m_job.get());

    // The host waits for the pool before it is destroyed, so the pointer is
    // valid here; the result is delivered in the GUI thread
    if (m_host)
    {
        PDFThreadAnnotations::happensBefore(m_job.get());
        PDFThreadAnnotations::TransportScope transportScope;
        QMetaObject::invokeMethod(m_host.data(), &PDFAppMcpHost::onJobFinished, Qt::QueuedConnection, m_job);
    }
}

}   // namespace pdf
