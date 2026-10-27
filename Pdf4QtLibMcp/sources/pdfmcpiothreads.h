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

#ifndef PDFMCPIOTHREADS_H
#define PDFMCPIOTHREADS_H

#include "pdf4qtlibmcp_export.h"

#include <QObject>

#include <atomic>
#include <memory>
#include <vector>

class QThread;

namespace pdf
{

class PDFMcpServerShared;

/// Home of the connections of one I/O thread (lives in that thread): it creates
/// a PDFMcpConnection for each socket assigned to the thread and closes them
/// all at the stop of the server
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpIoWorker : public QObject
{
    Q_OBJECT

public:
    explicit PDFMcpIoWorker(std::shared_ptr<PDFMcpServerShared> shared);
    virtual ~PDFMcpIoWorker() override;

    /// Creates the connection of the socket (queued from the listener)
    void addConnection(qintptr socketDescriptor);

    /// Aborts and deletes every connection (blocking queued from the GUI thread at the stop)
    void closeAll();

private:
    std::shared_ptr<PDFMcpServerShared> m_shared;
};

/// Fixed pool of the I/O threads with event loops (plan 2.2): sockets are
/// assigned round robin; nobody blocks - a thread waits in its event loop, so it
/// sees a disconnect at once. Started and stopped by the server in the GUI
/// thread; dispatch is thread safe (the listener calls it in the control thread).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpIoThreads
{
public:
    PDFMcpIoThreads() = default;
    ~PDFMcpIoThreads();

    PDFMcpIoThreads(const PDFMcpIoThreads&) = delete;
    PDFMcpIoThreads& operator=(const PDFMcpIoThreads&) = delete;

    /// Starts \p count threads (at least 1)
    void start(int count, std::shared_ptr<PDFMcpServerShared> shared);

    /// Assigns a new socket to the next thread (the socket is closed, when the
    /// pool does not run)
    void dispatch(qintptr socketDescriptor);

    /// Closes every connection and joins the threads
    void stop();

    int getThreadCount() const { return int(m_threads.size()); }
    bool isRunning() const { return !m_threads.empty(); }

private:
    std::vector<QThread*> m_threads;
    std::vector<PDFMcpIoWorker*> m_workers;
    std::atomic<quint64> m_next = 0;
};

}   // namespace pdf

#endif // PDFMCPIOTHREADS_H
