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

#include "pdfmcptasks.h"
#include "pdfmcpjson.h"
#include "pdfmcplimits.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimeZone>

namespace pdf
{

QLatin1StringView PDFMcpTask::getStatusName(Status status)
{
    switch (status)
    {
        case Status::Working:
            return QLatin1StringView("working");
        case Status::InputRequired:
            return QLatin1StringView("input_required");
        case Status::Completed:
            return QLatin1StringView("completed");
        case Status::Canceled:
            return QLatin1StringView("cancelled");
        case Status::Failed:
            return QLatin1StringView("failed");
    }
    return QLatin1StringView("failed");
}

void PDFMcpTask::writeMembers(PDFMcpJsonWriter& writer, qint64 ttl, int pollInterval) const
{
    writer.writeMember(QLatin1StringView("taskId"), QStringView(taskId));
    writer.writeMember(QLatin1StringView("status"), getStatusName(status));
    if (!statusMessage.isEmpty())
    {
        writer.writeMember(QLatin1StringView("statusMessage"), QStringView(statusMessage));
    }
    writer.writeMember(QLatin1StringView("createdAt"), QStringView(PDFMcpTaskStore::formatTime(createdAt)));
    writer.writeMember(QLatin1StringView("lastUpdatedAt"), QStringView(PDFMcpTaskStore::formatTime(lastUpdatedAt)));

    // The time to live is counted from the creation: a running task has no
    // limit, a finished one is kept for the TTL after its end
    writer.key(QLatin1StringView("ttlMs"));
    if (endedAt > 0)
    {
        writer.writeInteger(qMax<qint64>(0, endedAt - createdAt) + ttl);
    }
    else
    {
        writer.writeNull();
    }
    writer.writeMember(QLatin1StringView("pollIntervalMs"), pollInterval);

    switch (status)
    {
        case Status::Completed:
            writer.key(QLatin1StringView("result"));
            writer.writeRaw(result.isEmpty() ? QByteArrayView("{}") : QByteArrayView(result));
            break;
        case Status::Failed:
            writer.key(QLatin1StringView("error"));
            writer.writeRaw(error.isEmpty() ? QByteArrayView("{\"code\":-32603,\"message\":\"The task failed\"}") : QByteArrayView(error));
            break;
        case Status::InputRequired:
            writer.key(QLatin1StringView("inputRequests"));
            writer.writeRaw(inputRequests.isEmpty() ? QByteArrayView("{}") : QByteArrayView(inputRequests));
            break;
        case Status::Working:
        case Status::Canceled:
            break;
    }
}

PDFMcpTask* PDFMcpTaskStore::create(const QString& assistantKey, qint64 now)
{
    if (int(m_tasks.size()) >= PDFMcpLimits::MAXIMUM_TASKS || getTaskCount(assistantKey) >= PDFMcpLimits::MAXIMUM_TASKS_PER_ASSISTANT)
    {
        return nullptr;
    }

    QString taskId;
    do
    {
        quint32 words[4] = { };
        QRandomGenerator::system()->fillRange(words, 4);
        QByteArray bytes(reinterpret_cast<const char*>(words), sizeof(words));
        taskId = QString::fromLatin1(bytes.toHex());
    }
    while (m_tasks.count(taskId) > 0);

    std::unique_ptr<PDFMcpTask> task = std::make_unique<PDFMcpTask>();
    task->taskId = taskId;
    task->assistantKey = assistantKey;
    task->createdAt = now;
    task->lastUpdatedAt = now;
    PDFMcpTask* result = task.get();
    m_tasks[taskId] = std::move(task);
    return result;
}

PDFMcpTask* PDFMcpTaskStore::find(const QString& taskId)
{
    auto it = m_tasks.find(taskId);
    return it != m_tasks.end() ? it->second.get() : nullptr;
}

PDFMcpTask* PDFMcpTaskStore::findOwned(const QString& taskId, const QString& assistantKey)
{
    PDFMcpTask* task = find(taskId);
    return (task && task->assistantKey == assistantKey) ? task : nullptr;
}

int PDFMcpTaskStore::purge(qint64 now, qint64 ttl)
{
    int count = 0;
    for (auto it = m_tasks.begin(); it != m_tasks.end();)
    {
        const PDFMcpTask& task = *it->second;
        if (task.isTerminal() && task.endedAt > 0 && now - task.endedAt >= ttl)
        {
            it = m_tasks.erase(it);
            ++count;
        }
        else
        {
            ++it;
        }
    }
    return count;
}

void PDFMcpTaskStore::clear()
{
    m_tasks.clear();
}

int PDFMcpTaskStore::getTaskCount(const QString& assistantKey) const
{
    int count = 0;
    for (const auto& item : m_tasks)
    {
        if (item.second->assistantKey == assistantKey)
        {
            ++count;
        }
    }
    return count;
}

std::vector<QString> PDFMcpTaskStore::getTaskIds() const
{
    std::vector<QString> ids;
    for (const auto& item : m_tasks)
    {
        ids.push_back(item.first);
    }
    return ids;
}

QString PDFMcpTaskStore::formatTime(qint64 time)
{
    return QDateTime::fromMSecsSinceEpoch(time, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}

}   // namespace pdf
