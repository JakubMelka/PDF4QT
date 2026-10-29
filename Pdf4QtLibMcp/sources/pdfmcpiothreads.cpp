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

#include "pdfmcpiothreads.h"
#include "pdfmcpconnection.h"
#include "pdfmcpserver.h"
#include "pdfthreadannotations.h"

#include <QAbstractSocket>
#include <QThread>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace pdf
{

/// Helpers of the I/O threads
class PDFMcpIoThreadsHelper
{
public:
    PDFMcpIoThreadsHelper() = delete;

    /// Closes a native socket, which no connection took
    static void closeDescriptor(qintptr socketDescriptor)
    {
#ifdef Q_OS_WIN
        ::closesocket(SOCKET(socketDescriptor));
#else
        ::close(int(socketDescriptor));
#endif
    }
};

PDFMcpIoWorker::PDFMcpIoWorker(std::shared_ptr<PDFMcpServerShared> shared) :
    m_shared(std::move(shared))
{

}

PDFMcpIoWorker::~PDFMcpIoWorker() = default;

void PDFMcpIoWorker::addConnection(qintptr socketDescriptor)
{
    // The socket accepted by the listener of another thread (see PDFMcpIoThreads::dispatch)
    PDFThreadAnnotations::happensAfter(this);
    PDFMcpConnection* connection = new PDFMcpConnection(m_shared, this);
    if (!connection->start(socketDescriptor))
    {
        delete connection;
        PDFMcpIoThreadsHelper::closeDescriptor(socketDescriptor);
    }
}

void PDFMcpIoWorker::closeAll()
{
    const QList<PDFMcpConnection*> connections = findChildren<PDFMcpConnection*>(Qt::FindDirectChildrenOnly);
    for (PDFMcpConnection* connection : connections)
    {
        connection->abort();
        delete connection;
    }

    // The caller waits for the end of this slot inside of Qt (BlockingQueuedConnection)
    PDFThreadAnnotations::happensBefore(this);
}

PDFMcpIoThreads::~PDFMcpIoThreads()
{
    stop();
}

void PDFMcpIoThreads::start(int count, std::shared_ptr<PDFMcpServerShared> shared)
{
    stop();

    count = qMax(1, count);
    for (int i = 0; i < count; ++i)
    {
        QThread* thread = new QThread();
        thread->setObjectName(QStringLiteral("PDF4QT MCP I/O %1").arg(i + 1));
        PDFMcpIoWorker* worker = new PDFMcpIoWorker(shared);
        worker->moveToThread(thread);
        thread->start();
        m_threads.push_back(thread);
        m_workers.push_back(worker);
    }
}

void PDFMcpIoThreads::dispatch(qintptr socketDescriptor)
{
    if (m_workers.empty())
    {
        PDFMcpIoThreadsHelper::closeDescriptor(socketDescriptor);
        return;
    }

    const size_t index = size_t(m_next++ % m_workers.size());
    PDFThreadAnnotations::happensBefore(m_workers[index]);
    PDFThreadAnnotations::TransportScope transportScope;
    QMetaObject::invokeMethod(m_workers[index], &PDFMcpIoWorker::addConnection, Qt::QueuedConnection, socketDescriptor);
}

void PDFMcpIoThreads::stop()
{
    for (size_t i = 0; i < m_workers.size(); ++i)
    {
        if (m_threads[i]->isRunning())
        {
            {
                PDFThreadAnnotations::TransportScope transportScope;
                QMetaObject::invokeMethod(m_workers[i], &PDFMcpIoWorker::closeAll, Qt::BlockingQueuedConnection);
            }
            PDFThreadAnnotations::happensAfter(m_workers[i]);
        }
        m_threads[i]->quit();
    }

    for (size_t i = 0; i < m_workers.size(); ++i)
    {
        m_threads[i]->wait();

        // The thread has ended: its objects (also the connections, which arrived
        // by a queued addConnection after closeAll) are deleted here
        delete m_workers[i];
        delete m_threads[i];
    }

    m_workers.clear();
    m_threads.clear();
}

}   // namespace pdf
