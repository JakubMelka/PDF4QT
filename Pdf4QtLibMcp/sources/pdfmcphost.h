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

#ifndef PDFMCPHOST_H
#define PDFMCPHOST_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcpcall.h"

#include <QObject>
#include <QString>

#include <memory>

namespace pdf
{

/// What happened to an assistant [D2] (plan 2.7)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpAssistantEvent
{
public:
    enum class Kind
    {
        Connected,      ///< First request after the assistant was not connected
        Idle,           ///< No request for 30 min and no listen stream
        Disconnected,   ///< Disconnected by the user (Disconnect / Disconnect All / a new token)
        SessionEnded    ///< A legacy session ended (DELETE, expiry, server stop)
    };

    Kind kind = Kind::Connected;
    QString key;                ///< Assistant key "a:…" / "s:…"
    QString name;               ///< clientInfo.name (sanitized)
    QString title;              ///< clientInfo.title (sanitized, may be empty)
    QString address;            ///< Remote address without the port
    bool isRemote = false;
    qint64 connectedAt = 0;     ///< Milliseconds since the epoch
    PDFMcpEra era = PDFMcpEra::Modern;
};

/// Assistant known to the server (PDFMcpServer::getAssistants)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpAssistantInfo
{
public:
    QString key;
    QString name;
    QString title;
    QString address;
    bool isRemote = false;
    qint64 connectedAt = 0;
    qint64 lastSeen = 0;
    PDFMcpEra era = PDFMcpEra::Modern;
};

/// The application side of the MCP server (plan 2.2): it lives in the GUI
/// thread and is called only there, by queued invocations of the server. It
/// owns nothing of the server; the server must be stopped before the host is
/// destroyed (plan 2.6). Calls are answered by PDFMcpCall::finish (also from a
/// worker thread), never by a return value.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpHost : public QObject
{
    Q_OBJECT

public:
    explicit PDFMcpHost(QObject* parent = nullptr);
    virtual ~PDFMcpHost() override;

    /// tools/call, resources/read of a document resource, prompts/get, completion/complete
    virtual void handleCall(const std::shared_ptr<pdf::PDFMcpCall>& call) = 0;

    /// The call was canceled by the transport, the assistant or the server (the
    /// default does nothing; jobs see the cancellation by PDFOperationControl)
    virtual void callCanceled(const std::shared_ptr<pdf::PDFMcpCall>& call);

    /// An assistant was connected / became idle / was disconnected / its session ended
    virtual void assistantEvent(const pdf::PDFMcpAssistantEvent& event) = 0;
};

}   // namespace pdf

#endif // PDFMCPHOST_H
