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

#ifndef PDFMCPLISTENER_H
#define PDFMCPLISTENER_H

#include "pdf4qtlibmcp_export.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpServer>

#include <memory>
#include <vector>

namespace pdf
{

class PDFMcpDispatcher;
class PDFMcpIoThreads;
class PDFMcpServerShared;

/// Listening socket of the server (control thread): each accepted socket is
/// handed to the pool of the I/O threads by its descriptor
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpListener : public QTcpServer
{
    Q_OBJECT

public:
    PDFMcpListener(PDFMcpIoThreads* ioThreads, QObject* parent);
    virtual ~PDFMcpListener() override;

protected:
    virtual void incomingConnection(qintptr socketDescriptor) override;

private:
    PDFMcpIoThreads* m_ioThreads = nullptr;
};

/// The object of the control thread of one run of the server: it binds the
/// listeners (plan 2.6: 127.0.0.1 and ::1 locally, any address remotely) and
/// owns the dispatcher (package A). Created by PDFMcpServer, moved to the
/// control thread, deleted after the thread has ended.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpControl : public QObject
{
    Q_OBJECT

public:
    PDFMcpControl(quint64 run, std::shared_ptr<PDFMcpServerShared> shared, PDFMcpIoThreads* ioThreads);
    virtual ~PDFMcpControl() override;

    /// Binds the listeners (queued from the GUI thread); emits listenFinished
    void listen();

    /// Closes the listeners and stops the dispatcher (blocking queued from the GUI thread)
    void shutdown();

    PDFMcpDispatcher* getDispatcher() const;

    /// The error of the bind of [::1]:port is ignored only when the computer has
    /// no IPv6 loopback (unsupported operation, address not available); a taken
    /// ::1 (another program would get the requests and the token of a client
    /// resolving localhost to ::1) is a port in use
    static bool isIgnorableIPv6Error(QAbstractSocket::SocketError error);

Q_SIGNALS:
    /// Result of listen: \p socketError is QAbstractSocket::SocketError of a failure
    void listenFinished(quint64 run, bool ok, int socketError, const QString& message, quint16 port);

private:
    quint64 m_run = 0;
    std::shared_ptr<PDFMcpServerShared> m_shared;
    PDFMcpIoThreads* m_ioThreads = nullptr;
    std::vector<QPointer<PDFMcpListener>> m_listeners;
    QPointer<PDFMcpDispatcher> m_dispatcher;
};

}   // namespace pdf

#endif // PDFMCPLISTENER_H
