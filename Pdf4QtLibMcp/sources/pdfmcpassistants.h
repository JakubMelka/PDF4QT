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

#ifndef PDFMCPASSISTANTS_H
#define PDFMCPASSISTANTS_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcphost.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <map>
#include <vector>

namespace pdf
{

/// Clock of the MCP server (milliseconds since the epoch): the system time at
/// the first use advanced by a monotonic clock - a change of the system time
/// (a correction of the time, a manual change) never ends every session and
/// assistant at once or prolongs a block -, plus an offset, which the tests set
/// to move the time forward (the 30 minutes of an idle assistant or session,
/// the 10 minutes of a finished task, the 5 minutes of a sealed request state,
/// the rate limits). Thread safe.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpClock
{
public:
    PDFMcpClock() = delete;

    static qint64 now();

    /// Offset added to the system time (tests only)
    static void setOffset(qint64 offset);
    static qint64 getOffset();
};

/// Assistants of the server [D2] (plan 2.7): modern assistants by their keys
/// (first seen, last seen, open listen streams, running calls), legacy
/// assistants by their sessions (the dispatcher adds and removes them with the
/// sessions). An assistant is connected while it made a request in the last 30
/// minutes, holds a listen stream or has a running call; "connected at" is its
/// first request after it was not connected. Lives in the control thread.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpAssistantRegistry : public QObject
{
    Q_OBJECT

public:
    explicit PDFMcpAssistantRegistry(QObject* parent = nullptr);
    virtual ~PDFMcpAssistantRegistry() override;

    /// Display name of a client without clientInfo
    static QString getDefaultClientName() { return QStringLiteral("MCP client"); }

    /// Modern key "a:" + the first 32 hex digits of SHA-256(token 0 name 0 address)
    /// (an empty token without a token; the default name without clientInfo).
    /// Raw tokens are never stored - only this key.
    static QString createModernKey(const QByteArray& token, const QString& clientName, const QString& remoteAddress);

    /// Legacy key "s:" + the first 32 hex digits of SHA-256(session id)
    static QString createLegacyKey(const QByteArray& sessionId);

    /// The key has the form of an assistant key ("a:" / "s:" + 32 lower hex digits)
    static bool isValidKey(const QString& key);

    /// A request of the assistant (\p info: key, names, address, era); returns
    /// true, when the assistant was not connected (the event Connected), and
    /// emits assistantConnected then
    bool touch(const PDFMcpAssistantInfo& info, qint64 now);

    /// The assistant is known (connected)
    bool contains(const QString& key) const;

    /// Open listen streams and running calls keep an assistant connected
    void addStream(const QString& key);
    void removeStream(const QString& key);
    void addCall(const QString& key);
    void removeCall(const QString& key);
    int getStreamCount(const QString& key) const;
    int getCallCount(const QString& key) const;

    /// Removes the assistant (disconnect, end of a legacy session); false = unknown
    bool remove(const QString& key);

    /// Modern assistants idle for \p idleTime (no request, no stream, no call)
    /// are removed; their events Idle are returned (and emitted by assistantIdle)
    std::vector<PDFMcpAssistantEvent> removeIdle(qint64 now, qint64 idleTime);

    /// Removes every assistant
    void clear();

    std::vector<PDFMcpAssistantInfo> getAssistants() const;

    /// Event of the assistant (kind given; empty key for an unknown one)
    PDFMcpAssistantEvent createEvent(const QString& key, PDFMcpAssistantEvent::Kind kind) const;

    int getAssistantCount() const { return int(m_entries.size()); }

Q_SIGNALS:
    void assistantConnected(const pdf::PDFMcpAssistantEvent& event);
    void assistantIdle(const pdf::PDFMcpAssistantEvent& event);

private:
    class Entry
    {
    public:
        PDFMcpAssistantInfo info;
        int streams = 0;
        int calls = 0;
    };

    static PDFMcpAssistantEvent createEvent(const Entry& entry, PDFMcpAssistantEvent::Kind kind);

    std::map<QString, Entry> m_entries;
};

}   // namespace pdf

#endif // PDFMCPASSISTANTS_H
