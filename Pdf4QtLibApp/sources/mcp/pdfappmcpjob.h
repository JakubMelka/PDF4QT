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

#ifndef PDFAPPMCPJOB_H
#define PDFAPPMCPJOB_H

#include "pdfappglobal.h"
#include "pdfappdocument.h"

#include "pdfmcpcall.h"

#include <QPointer>
#include <QRunnable>

#include <memory>


namespace pdf
{

class PDFAppMcpHost;

/// Work of a call in an MCP worker (plan 2.3, 2.4): text, search, render and
/// every builder of a changed document run here on a snapshot of the document
/// (never on the document of the tab). execute runs in a worker thread and
/// touches only the snapshot and its own data; finish runs afterwards in the
/// GUI thread - only when the host still exists (the host waits for its jobs
/// before it is destroyed). The call is the PDFOperationControl of the work: a
/// canceled call stops it; a call canceled before its job ran is finished
/// without running it. An exception of execute finishes the call by an
/// execution error (PDFAppMcpJobRunnable::execute).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpJob : public std::enable_shared_from_this<PDFAppMcpJob>
{
public:
    PDFAppMcpJob(std::shared_ptr<PDFMcpCall> call, PDFAppDocumentSnapshot snapshot);
    virtual ~PDFAppMcpJob();

    PDFAppMcpJob(const PDFAppMcpJob&) = delete;
    PDFAppMcpJob& operator=(const PDFAppMcpJob&) = delete;

    const std::shared_ptr<PDFMcpCall>& getCall() const { return m_call; }
    const PDFAppDocumentSnapshot& getSnapshot() const { return m_snapshot; }

    /// A heavy job (render, a big builder): at most PDFMcpLimits::MAXIMUM_HEAVY_JOBS run
    /// at once, the others wait in the host (PDFAppMcpHost::startJob); constant for a job
    virtual bool isHeavy() const { return false; }

    /// The work (worker thread)
    virtual void execute() = 0;

    /// The result in the GUI thread (the default does nothing - execute has
    /// finished the call)
    virtual void finish(PDFAppMcpHost* host);

private:
    std::shared_ptr<PDFMcpCall> m_call;
    PDFAppDocumentSnapshot m_snapshot;
};

/// The QRunnable of a job (the thread pool of the host deletes it)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpJobRunnable : public QRunnable
{
public:
    PDFAppMcpJobRunnable(std::shared_ptr<PDFAppMcpJob> job, PDFAppMcpHost* host);
    virtual ~PDFAppMcpJobRunnable() override;

    virtual void run() override;

    /// Longest exception message given to the client
    static constexpr int MAXIMUM_MESSAGE_LENGTH = 256;

    /// Runs the job in the current thread: a call canceled before the job ran is
    /// not executed (finished in the GUI thread by the job or by the host), an
    /// exception of execute finishes the call by a failure - the message of a
    /// PDFException sanitized and bounded, any other exception by a generic text
    /// (never its own text)
    static void execute(PDFAppMcpJob* job);

    /// A failure of the call: an execution error of a tool, a protocol error of
    /// the other requests
    static PDFMcpCallResult createFailure(const PDFMcpCall& call, const QString& text);

private:
    std::shared_ptr<PDFAppMcpJob> m_job;
    QPointer<PDFAppMcpHost> m_host;
};

}   // namespace pdf

#endif // PDFAPPMCPJOB_H
