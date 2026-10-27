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

#include "pdfmcpsubscriptions.h"
#include "pdfmcpjson.h"
#include "pdfmcplimits.h"
#include "pdfmcpuritemplate.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpSubscriptionFilter
// ----------------------------------------------------------------------------

std::optional<PDFMcpSubscriptionFilter> PDFMcpSubscriptionFilter::parse(const QJsonValue& value, QString* error)
{
    if (!value.isObject())
    {
        if (error)
        {
            *error = QStringLiteral("params.notifications must be an object");
        }
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    PDFMcpSubscriptionFilter filter;

    struct Flag
    {
        const char* name;
        bool* target;
    };

    const Flag flags[] = {
        { "toolsListChanged", &filter.toolsListChanged },
        { "promptsListChanged", &filter.promptsListChanged },
        { "resourcesListChanged", &filter.resourcesListChanged }
    };

    for (const Flag& flag : flags)
    {
        const QJsonValue item = object.value(QLatin1StringView(flag.name));
        if (item.isUndefined())
        {
            continue;
        }
        if (!item.isBool())
        {
            if (error)
            {
                *error = QStringLiteral("params.notifications.%1 must be a boolean").arg(QLatin1StringView(flag.name));
            }
            return std::nullopt;
        }
        *flag.target = item.toBool();
    }

    const QJsonValue resources = object.value(QStringLiteral("resourceSubscriptions"));
    if (!resources.isUndefined())
    {
        if (!resources.isArray())
        {
            if (error)
            {
                *error = QStringLiteral("params.notifications.resourceSubscriptions must be an array of strings");
            }
            return std::nullopt;
        }

        // Invalid URIs are omitted from the acknowledged subset (not an error);
        // duplicates are found by a set (a list of 100 000 items is linear -
        // follow-up 23), the rest of a full list is not read
        const QJsonArray array = resources.toArray();
        QSet<QString> accepted;
        for (const QJsonValue& item : array)
        {
            if (filter.resourceSubscriptions.size() >= PDFMcpLimits::MAXIMUM_SUBSCRIPTIONS_PER_STREAM)
            {
                break;
            }

            const QString uri = item.toString();
            if (item.isString() && !accepted.contains(uri) && PDFMcpSubscriptions::isValidUri(uri))
            {
                accepted.insert(uri);
                filter.resourceSubscriptions.append(uri);
            }
        }
    }

    const QJsonValue tasks = object.value(QStringLiteral("taskIds"));
    if (!tasks.isUndefined())
    {
        if (!tasks.isArray())
        {
            if (error)
            {
                *error = QStringLiteral("params.notifications.taskIds must be an array of strings");
            }
            return std::nullopt;
        }

        filter.hasTaskIds = true;
        const QJsonArray array = tasks.toArray();
        QSet<QString> accepted;
        for (const QJsonValue& item : array)
        {
            if (filter.taskIds.size() >= PDFMcpLimits::MAXIMUM_SUBSCRIPTIONS_PER_STREAM)
            {
                break;
            }

            const QString taskId = item.toString();
            if (item.isString() && !taskId.isEmpty() && taskId.size() <= 64 && !accepted.contains(taskId))
            {
                accepted.insert(taskId);
                filter.taskIds.append(taskId);
            }
        }
    }

    return filter;
}

void PDFMcpSubscriptionFilter::write(PDFMcpJsonWriter& writer) const
{
    writer.beginObject();
    if (toolsListChanged)
    {
        writer.writeMember(QLatin1StringView("toolsListChanged"), true);
    }
    if (promptsListChanged)
    {
        writer.writeMember(QLatin1StringView("promptsListChanged"), true);
    }
    if (resourcesListChanged)
    {
        writer.writeMember(QLatin1StringView("resourcesListChanged"), true);
    }
    if (!resourceSubscriptions.isEmpty())
    {
        writer.key(QLatin1StringView("resourceSubscriptions"));
        writer.writeStringArray(resourceSubscriptions);
    }
    if (hasTaskIds)
    {
        writer.key(QLatin1StringView("taskIds"));
        writer.writeStringArray(taskIds);
    }
    writer.endObject();
}

// ----------------------------------------------------------------------------
// PDFMcpSubscriptions
// ----------------------------------------------------------------------------

bool PDFMcpSubscriptions::isValidUri(const QString& uri)
{
    if (uri.size() > PDFMcpUriTemplate::MAXIMUM_URI_LENGTH || !uri.startsWith(QLatin1String("pdf4qt://")) || uri.size() <= 9)
    {
        return false;
    }

    for (QChar c : uri)
    {
        const char16_t code = c.unicode();
        if (code < 0x21 || code == 0x7F || (code >= 0x80 && code < 0xA0) || c.isSurrogate() || PDFMcpJson::isBidiControl(code))
        {
            return false;
        }
    }
    return true;
}

bool PDFMcpSubscriptions::isAffected(const QString& subscribedUri, const QString& updatedUri)
{
    if (subscribedUri == updatedUri)
    {
        return true;
    }

    // A prefix of whole path segments ("pdf4qt://documents/ABC" updates
    // "pdf4qt://documents/ABC/info", never "pdf4qt://documents/ABCD/info")
    return !updatedUri.isEmpty() && subscribedUri.size() > updatedUri.size() && subscribedUri.startsWith(updatedUri) &&
           (updatedUri.endsWith(QLatin1Char('/')) || subscribedUri[updatedUri.size()] == QLatin1Char('/'));
}

bool PDFMcpSubscriptions::add(const QString& uri)
{
    if (!isValidUri(uri))
    {
        return false;
    }
    if (m_uris.contains(uri))
    {
        return true;
    }
    if (m_uris.size() >= PDFMcpLimits::MAXIMUM_SUBSCRIPTIONS_PER_STREAM)
    {
        return false;
    }
    m_uris.append(uri);
    return true;
}

bool PDFMcpSubscriptions::remove(const QString& uri)
{
    m_lastSent.erase(uri);
    m_pending.erase(uri);
    return m_uris.removeAll(uri) > 0;
}

QStringList PDFMcpSubscriptions::update(const QString& updatedUri, qint64 now, qint64 interval)
{
    QStringList result;
    for (const QString& uri : m_uris)
    {
        if (!isAffected(uri, updatedUri))
        {
            continue;
        }

        auto it = m_lastSent.find(uri);
        if (it == m_lastSent.end() || now - it->second >= interval)
        {
            m_lastSent[uri] = now;
            m_pending.erase(uri);
            result.append(uri);
        }
        else
        {
            m_pending.insert(uri);
        }
    }
    return result;
}

QStringList PDFMcpSubscriptions::takeDue(qint64 now, qint64 interval)
{
    QStringList result;
    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        auto lastIt = m_lastSent.find(*it);
        if (lastIt == m_lastSent.end() || now - lastIt->second >= interval)
        {
            m_lastSent[*it] = now;
            result.append(*it);
            it = m_pending.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return result;
}

}   // namespace pdf
