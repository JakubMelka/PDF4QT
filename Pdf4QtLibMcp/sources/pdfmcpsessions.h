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

#ifndef PDFMCPSESSIONS_H
#define PDFMCPSESSIONS_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcpcall.h"
#include "pdfmcpsubscriptions.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <map>
#include <memory>
#include <vector>

namespace pdf
{

class PDFMcpExchange;

/// One legacy session of 2025-11-25 / 2025-06-18 (plan 1.3). The session id
/// itself is never stored: the session is found by its key "s:" + SHA-256 of
/// the id (PDFMcpAssistantRegistry::createLegacyKey).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpSession
{
public:
    QString key;                            ///< "s:<32 hex>" - also the assistant key
    PDFMcpEra era = PDFMcpEra::Legacy20251125;
    QString protocolVersion;                ///< The negotiated version
    QJsonObject clientCapabilities;
    QString clientName;                     ///< Sanitized clientInfo.name ("MCP client" without it)
    QString clientTitle;
    QString clientVersion;
    QString remoteAddress;
    bool isRemote = false;
    QString logLevel;                       ///< logging/setLevel, empty = no notifications/message
    bool isInitialized = false;             ///< notifications/initialized arrived
    bool isUsed = false;                    ///< Another message of the session arrived after initialize (a request, a GET stream, an answer)
    qint64 createdAt = 0;
    qint64 lastSeen = 0;
    std::shared_ptr<PDFMcpExchange> stream; ///< The GET stream (at most one)
    PDFMcpSubscriptions subscriptions;      ///< resources/subscribe
    qint64 nextServerRequestId = 1;         ///< Ids of the requests of the server (elicitation/create)

    /// The client context of a request of the session
    PDFMcpClientContext createClientContext() const;
};

/// Legacy sessions (plan 1.3): Mcp-Session-Id = 128 random bits in base32
/// (visible ASCII), at most 64 sessions; a session ends after 30 minutes
/// without a request (an open GET stream keeps it), at DELETE, at Disconnect,
/// at a new token or at the stop. Lives in the control thread of the
/// dispatcher (no locks).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpSessionRegistry
{
public:
    PDFMcpSessionRegistry() = default;
    PDFMcpSessionRegistry(const PDFMcpSessionRegistry&) = delete;
    PDFMcpSessionRegistry& operator=(const PDFMcpSessionRegistry&) = delete;

    /// A new random session id (26 characters of base32)
    static QByteArray createSessionId();

    /// The text has the form of a session id of this server (base32, 26 characters)
    static bool isValidSessionId(QByteArrayView sessionId);

    /// Creates a session of the id (nullptr when the limit is reached)
    PDFMcpSession* create(const QByteArray& sessionId, qint64 now);

    /// The session of a session id presented by a client (nullptr = unknown)
    PDFMcpSession* find(QByteArrayView sessionId);

    /// The session of its key (nullptr = unknown)
    PDFMcpSession* findByKey(const QString& key);

    /// Removes a session (its key); the session object is returned to the caller
    std::unique_ptr<PDFMcpSession> remove(const QString& key);

    /// Keys of the sessions idle for \p idleTime (no request, no GET stream)
    std::vector<QString> getIdleSessions(qint64 now, qint64 idleTime) const;

    /// Keys of the sessions created \p unusedTime ago, which got no message
    /// after their initialize (PDFMcpLimits::UNUSED_SESSION_TIME)
    std::vector<QString> getUnusedSessions(qint64 now, qint64 unusedTime) const;

    /// Keys of every session
    std::vector<QString> getKeys() const;

    int getSessionCount() const { return int(m_sessions.size()); }

    /// Every session (the dispatcher iterates them for the notifications)
    const std::map<QString, std::unique_ptr<PDFMcpSession>>& getSessions() const { return m_sessions; }

private:
    std::map<QString, std::unique_ptr<PDFMcpSession>> m_sessions;
};

}   // namespace pdf

#endif // PDFMCPSESSIONS_H
