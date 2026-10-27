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

#include "pdfmcpsessions.h"
#include "pdfmcpassistants.h"
#include "pdfmcpconnection.h"
#include "pdfmcplimits.h"

#include <QRandomGenerator>

namespace pdf
{

PDFMcpClientContext PDFMcpSession::createClientContext() const
{
    PDFMcpClientContext client;
    client.era = era;
    client.protocolVersion = protocolVersion;
    client.clientCapabilities = clientCapabilities;
    client.clientName = clientName;
    client.clientTitle = clientTitle;
    client.clientVersion = clientVersion;
    client.remoteAddress = remoteAddress;
    client.isRemote = isRemote;
    client.logLevel = logLevel;
    client.assistantKey = key;
    client.sessionKey = key;
    return client;
}

QByteArray PDFMcpSessionRegistry::createSessionId()
{
    // 128 random bits in base32 of RFC 4648 without padding (26 characters)
    static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    quint32 words[4] = { };
    QRandomGenerator::system()->fillRange(words, 4);

    QByteArray bytes;
    for (quint32 word : words)
    {
        bytes.append(char(word >> 24));
        bytes.append(char(word >> 16));
        bytes.append(char(word >> 8));
        bytes.append(char(word));
    }

    QByteArray text;
    text.reserve(26);
    quint32 buffer = 0;
    int bits = 0;
    for (char byte : bytes)
    {
        buffer = (buffer << 8) | uchar(byte);
        bits += 8;
        while (bits >= 5)
        {
            text.append(ALPHABET[(buffer >> (bits - 5)) & 0x1F]);
            bits -= 5;
        }
    }
    if (bits > 0)
    {
        text.append(ALPHABET[(buffer << (5 - bits)) & 0x1F]);
    }
    return text;
}

bool PDFMcpSessionRegistry::isValidSessionId(QByteArrayView sessionId)
{
    if (sessionId.size() != 26)
    {
        return false;
    }

    for (char c : sessionId)
    {
        if (!((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7')))
        {
            return false;
        }
    }
    return true;
}

PDFMcpSession* PDFMcpSessionRegistry::create(const QByteArray& sessionId, qint64 now)
{
    if (int(m_sessions.size()) >= PDFMcpLimits::MAXIMUM_LEGACY_SESSIONS)
    {
        return nullptr;
    }

    std::unique_ptr<PDFMcpSession> session = std::make_unique<PDFMcpSession>();
    session->key = PDFMcpAssistantRegistry::createLegacyKey(sessionId);
    session->createdAt = now;
    session->lastSeen = now;
    PDFMcpSession* result = session.get();
    m_sessions[session->key] = std::move(session);
    return result;
}

PDFMcpSession* PDFMcpSessionRegistry::find(QByteArrayView sessionId)
{
    if (!isValidSessionId(sessionId))
    {
        return nullptr;
    }
    return findByKey(PDFMcpAssistantRegistry::createLegacyKey(sessionId.toByteArray()));
}

PDFMcpSession* PDFMcpSessionRegistry::findByKey(const QString& key)
{
    auto it = m_sessions.find(key);
    return it != m_sessions.end() ? it->second.get() : nullptr;
}

std::unique_ptr<PDFMcpSession> PDFMcpSessionRegistry::remove(const QString& key)
{
    auto it = m_sessions.find(key);
    if (it == m_sessions.end())
    {
        return nullptr;
    }

    std::unique_ptr<PDFMcpSession> session = std::move(it->second);
    m_sessions.erase(it);
    return session;
}

std::vector<QString> PDFMcpSessionRegistry::getIdleSessions(qint64 now, qint64 idleTime) const
{
    std::vector<QString> keys;
    for (const auto& item : m_sessions)
    {
        const PDFMcpSession& session = *item.second;
        const bool hasStream = session.stream && !session.stream->isClosed() && !session.stream->isFinished();
        if (!hasStream && now - session.lastSeen >= idleTime)
        {
            keys.push_back(item.first);
        }
    }
    return keys;
}

std::vector<QString> PDFMcpSessionRegistry::getKeys() const
{
    std::vector<QString> keys;
    for (const auto& item : m_sessions)
    {
        keys.push_back(item.first);
    }
    return keys;
}

}   // namespace pdf
