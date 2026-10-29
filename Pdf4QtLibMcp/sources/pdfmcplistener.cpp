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

#include "pdfmcplistener.h"
#include "pdfmcpdispatcher.h"
#include "pdfmcpiothreads.h"
#include "pdfmcpserver.h"
#include "pdfthreadannotations.h"

#include <QHostAddress>

namespace pdf
{

PDFMcpListener::PDFMcpListener(PDFMcpIoThreads* ioThreads, QObject* parent) :
    QTcpServer(parent),
    m_ioThreads(ioThreads)
{

}

PDFMcpListener::~PDFMcpListener() = default;

void PDFMcpListener::incomingConnection(qintptr socketDescriptor)
{
    m_ioThreads->dispatch(socketDescriptor);
}

PDFMcpControl::PDFMcpControl(quint64 run, std::shared_ptr<PDFMcpServerShared> shared, PDFMcpIoThreads* ioThreads) :
    m_run(run),
    m_shared(std::move(shared)),
    m_ioThreads(ioThreads)
{

}

PDFMcpControl::~PDFMcpControl() = default;

PDFMcpDispatcher* PDFMcpControl::getDispatcher() const
{
    return m_dispatcher;
}

void PDFMcpControl::listen()
{
    const PDFMcpServerConfig& config = m_shared->getConfig();
    m_dispatcher = new PDFMcpDispatcher(m_shared, this);

    const QHostAddress address = config.bindMode == PDFMcpBindMode::Remote ? QHostAddress(QHostAddress::Any) : QHostAddress(QHostAddress::LocalHost);

    // A free port (0, tests) is tried again, when its ::1 is taken by another program
    const int attempts = config.port == 0 ? 8 : 1;
    quint16 port = 0;
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        PDFMcpListener* primary = new PDFMcpListener(m_ioThreads, this);
        if (!primary->listen(address, config.port))
        {
            const int error = int(primary->serverError());
            const QString message = primary->errorString();
            delete primary;
            PDFThreadAnnotations::happensBefore(this);
            PDFThreadAnnotations::TransportScope transportScope;
            Q_EMIT listenFinished(m_run, false, error, message, config.port);
            return;
        }

        port = primary->serverPort();
        if (config.bindMode != PDFMcpBindMode::Local)
        {
            m_listeners.push_back(primary);
            break;
        }

        // ::1 of the same port: a client resolving localhost to ::1 first would
        // give its requests and its token to another program listening there, so
        // a taken ::1 is a port in use; only a computer without IPv6 is accepted
        PDFMcpListener* secondary = new PDFMcpListener(m_ioThreads, this);
        if (secondary->listen(QHostAddress(QHostAddress::LocalHostIPv6), port))
        {
            m_listeners.push_back(primary);
            m_listeners.push_back(secondary);
            break;
        }

        const QAbstractSocket::SocketError error = secondary->serverError();
        const QString message = secondary->errorString();
        delete secondary;
        if (isIgnorableIPv6Error(error))
        {
            m_listeners.push_back(primary);
            break;
        }

        primary->close();
        delete primary;
        if (attempt + 1 == attempts || error != QAbstractSocket::AddressInUseError)
        {
            PDFThreadAnnotations::happensBefore(this);
            PDFThreadAnnotations::TransportScope transportScope;
            Q_EMIT listenFinished(m_run, false, int(error), message, config.port);
            return;
        }
    }

    if (m_dispatcher)
    {
        m_dispatcher->setPort(port);
    }
    PDFThreadAnnotations::happensBefore(this);
    PDFThreadAnnotations::TransportScope transportScope;
    Q_EMIT listenFinished(m_run, true, -1, QString(), port);
}

bool PDFMcpControl::isIgnorableIPv6Error(QAbstractSocket::SocketError error)
{
    switch (error)
    {
        case QAbstractSocket::UnsupportedSocketOperationError:
        case QAbstractSocket::SocketAddressNotAvailableError:
            return true;
        default:
            return false;
    }
}

void PDFMcpControl::shutdown()
{
    for (const QPointer<PDFMcpListener>& listener : m_listeners)
    {
        if (listener)
        {
            listener->close();
            delete listener.data();
        }
    }
    m_listeners.clear();

    if (m_dispatcher)
    {
        m_dispatcher->shutdown();
        delete m_dispatcher.data();
    }

    // The server waits for the end of this slot inside of Qt (BlockingQueuedConnection)
    PDFThreadAnnotations::happensBefore(this);
}

}   // namespace pdf
