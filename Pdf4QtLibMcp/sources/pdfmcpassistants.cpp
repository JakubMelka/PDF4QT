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

#include "pdfmcpassistants.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>

#include <atomic>

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpClock
// ----------------------------------------------------------------------------

/// Offset of the clock (tests)
class PDFMcpClockOffset
{
public:
    PDFMcpClockOffset() = delete;

    static std::atomic<qint64>& get()
    {
        static std::atomic<qint64> s_offset = 0;
        return s_offset;
    }
};

/// The monotonic base of the clock (initialized once, thread safe)
class PDFMcpClockBase
{
public:
    PDFMcpClockBase() :
        m_epochAtStart(QDateTime::currentMSecsSinceEpoch())
    {
        m_timer.start();
    }

    qint64 now() const
    {
        return m_epochAtStart + m_timer.elapsed();
    }

    static const PDFMcpClockBase& get()
    {
        static const PDFMcpClockBase s_base;
        return s_base;
    }

private:
    qint64 m_epochAtStart = 0;
    QElapsedTimer m_timer;
};

qint64 PDFMcpClock::now()
{
    return PDFMcpClockBase::get().now() + PDFMcpClockOffset::get().load();
}

void PDFMcpClock::setOffset(qint64 offset)
{
    PDFMcpClockOffset::get().store(offset);
}

qint64 PDFMcpClock::getOffset()
{
    return PDFMcpClockOffset::get().load();
}

// ----------------------------------------------------------------------------
// PDFMcpAssistantRegistry
// ----------------------------------------------------------------------------

PDFMcpAssistantRegistry::PDFMcpAssistantRegistry(QObject* parent) :
    QObject(parent)
{

}

PDFMcpAssistantRegistry::~PDFMcpAssistantRegistry() = default;

QString PDFMcpAssistantRegistry::createModernKey(const QByteArray& token, const QString& clientName, const QString& remoteAddress)
{
    const char separator = '\0';

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(token);
    hash.addData(QByteArrayView(&separator, 1));
    hash.addData((clientName.isEmpty() ? getDefaultClientName() : clientName).toUtf8());
    hash.addData(QByteArrayView(&separator, 1));
    hash.addData(remoteAddress.toUtf8());
    return QStringLiteral("a:") + QString::fromLatin1(hash.result().toHex().left(32));
}

QString PDFMcpAssistantRegistry::createLegacyKey(const QByteArray& sessionId)
{
    return QStringLiteral("s:") + QString::fromLatin1(QCryptographicHash::hash(sessionId, QCryptographicHash::Sha256).toHex().left(32));
}

bool PDFMcpAssistantRegistry::isValidKey(const QString& key)
{
    if (key.size() != 34 || (!key.startsWith(QLatin1String("a:")) && !key.startsWith(QLatin1String("s:"))))
    {
        return false;
    }

    for (qsizetype i = 2; i < key.size(); ++i)
    {
        const char16_t c = key[i].unicode();
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
        {
            return false;
        }
    }
    return true;
}

PDFMcpAssistantEvent PDFMcpAssistantRegistry::createEvent(const Entry& entry, PDFMcpAssistantEvent::Kind kind)
{
    PDFMcpAssistantEvent event;
    event.kind = kind;
    event.key = entry.info.key;
    event.name = entry.info.name;
    event.title = entry.info.title;
    event.address = entry.info.address;
    event.isRemote = entry.info.isRemote;
    event.connectedAt = entry.info.connectedAt;
    event.era = entry.info.era;
    return event;
}

PDFMcpAssistantEvent PDFMcpAssistantRegistry::createEvent(const QString& key, PDFMcpAssistantEvent::Kind kind) const
{
    auto it = m_entries.find(key);
    if (it != m_entries.end())
    {
        return createEvent(it->second, kind);
    }

    PDFMcpAssistantEvent event;
    event.kind = kind;
    event.key = key;
    return event;
}

bool PDFMcpAssistantRegistry::touch(const PDFMcpAssistantInfo& info, qint64 now)
{
    if (info.key.isEmpty())
    {
        return false;
    }

    auto it = m_entries.find(info.key);
    if (it != m_entries.end())
    {
        it->second.info.lastSeen = now;
        return false;
    }

    Entry entry;
    entry.info = info;
    entry.info.connectedAt = now;
    entry.info.lastSeen = now;
    it = m_entries.emplace(info.key, std::move(entry)).first;
    Q_EMIT assistantConnected(createEvent(it->second, PDFMcpAssistantEvent::Kind::Connected));
    return true;
}

bool PDFMcpAssistantRegistry::contains(const QString& key) const
{
    return m_entries.count(key) > 0;
}

void PDFMcpAssistantRegistry::addStream(const QString& key)
{
    auto it = m_entries.find(key);
    if (it != m_entries.end())
    {
        ++it->second.streams;
    }
}

void PDFMcpAssistantRegistry::removeStream(const QString& key)
{
    auto it = m_entries.find(key);
    if (it != m_entries.end() && it->second.streams > 0)
    {
        --it->second.streams;
    }
}

void PDFMcpAssistantRegistry::addCall(const QString& key)
{
    auto it = m_entries.find(key);
    if (it != m_entries.end())
    {
        ++it->second.calls;
    }
}

void PDFMcpAssistantRegistry::removeCall(const QString& key)
{
    auto it = m_entries.find(key);
    if (it != m_entries.end() && it->second.calls > 0)
    {
        --it->second.calls;
    }
}

int PDFMcpAssistantRegistry::getStreamCount(const QString& key) const
{
    auto it = m_entries.find(key);
    return it != m_entries.end() ? it->second.streams : 0;
}

int PDFMcpAssistantRegistry::getCallCount(const QString& key) const
{
    auto it = m_entries.find(key);
    return it != m_entries.end() ? it->second.calls : 0;
}

bool PDFMcpAssistantRegistry::remove(const QString& key)
{
    return m_entries.erase(key) > 0;
}

std::vector<PDFMcpAssistantEvent> PDFMcpAssistantRegistry::removeIdle(qint64 now, qint64 idleTime)
{
    std::vector<PDFMcpAssistantEvent> events;
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        const Entry& entry = it->second;
        const bool isModern = entry.info.era == PDFMcpEra::Modern;
        if (isModern && entry.streams == 0 && entry.calls == 0 && now - entry.info.lastSeen >= idleTime)
        {
            events.push_back(createEvent(entry, PDFMcpAssistantEvent::Kind::Idle));
            it = m_entries.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (const PDFMcpAssistantEvent& event : events)
    {
        Q_EMIT assistantIdle(event);
    }
    return events;
}

void PDFMcpAssistantRegistry::clear()
{
    m_entries.clear();
}

std::vector<PDFMcpAssistantInfo> PDFMcpAssistantRegistry::getAssistants() const
{
    std::vector<PDFMcpAssistantInfo> assistants;
    assistants.reserve(m_entries.size());
    for (const auto& item : m_entries)
    {
        assistants.push_back(item.second.info);
    }
    return assistants;
}

}   // namespace pdf
