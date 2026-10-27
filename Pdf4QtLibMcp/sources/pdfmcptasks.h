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

#ifndef PDFMCPTASKS_H
#define PDFMCPTASKS_H

#include "pdf4qtlibmcp_export.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <map>
#include <memory>
#include <vector>

namespace pdf
{

class PDFMcpJsonWriter;

/// One task of the extension io.modelcontextprotocol/tasks [D3] (plan 2.8)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpTask
{
public:
    enum class Status
    {
        Working,
        InputRequired,
        Completed,
        Canceled,               ///< Wire name "cancelled" (spec)
        Failed
    };

    QString taskId;                 ///< 32 random hex digits
    QString assistantKey;           ///< Only this assistant may get, update or cancel the task
    Status status = Status::Working;
    QString statusMessage;
    qint64 createdAt = 0;
    qint64 lastUpdatedAt = 0;
    qint64 endedAt = 0;             ///< 0 while the task runs
    QByteArray result;              ///< Completed: the result object of the call (without the era members)
    QByteArray error;               ///< Failed: the JSON-RPC error object
    QByteArray inputRequests;       ///< InputRequired: the inputRequests object
    QJsonObject allowedValues;      ///< InputRequired: the values allowed in the answers
    quint64 callId = 0;             ///< The pending call of the dispatcher (0 = none)

    bool isTerminal() const { return status == Status::Completed || status == Status::Canceled || status == Status::Failed; }

    /// Name of a status ("working", "input_required", ...)
    static QLatin1StringView getStatusName(Status status);

    /// Writes the members of the task (DetailedTask: taskId, status,
    /// statusMessage, createdAt, lastUpdatedAt, ttlMs, pollIntervalMs and
    /// result / error / inputRequests by the status) into the current object
    /// of the writer; \p ttl is the time a finished task is kept
    void writeMembers(PDFMcpJsonWriter& writer, qint64 ttl, int pollInterval) const;
};

/// Tasks of a run of the server (plan 2.8): created in memory before the reply
/// ("durably created"), bound to the assistant, at most 64 per assistant and
/// 256 in total, a finished task is kept for its TTL (10 minutes). Lives in
/// the control thread of the dispatcher.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpTaskStore
{
public:
    PDFMcpTaskStore() = default;
    PDFMcpTaskStore(const PDFMcpTaskStore&) = delete;
    PDFMcpTaskStore& operator=(const PDFMcpTaskStore&) = delete;

    /// A new task (nullptr when a limit is reached)
    PDFMcpTask* create(const QString& assistantKey, qint64 now);

    /// The task of the id (nullptr = unknown)
    PDFMcpTask* find(const QString& taskId);

    /// The task of the id, when it belongs to the assistant (nullptr otherwise:
    /// a foreign task is the same as an unknown one)
    PDFMcpTask* findOwned(const QString& taskId, const QString& assistantKey);

    /// Removes the finished tasks older than \p ttl; returns their number
    int purge(qint64 now, qint64 ttl);

    /// Removes every task
    void clear();

    int getTaskCount() const { return int(m_tasks.size()); }
    int getTaskCount(const QString& assistantKey) const;

    /// Ids of every task
    std::vector<QString> getTaskIds() const;

    /// ISO 8601 text of a time (UTC, milliseconds)
    static QString formatTime(qint64 time);

private:
    std::map<QString, std::unique_ptr<PDFMcpTask>> m_tasks;
};

}   // namespace pdf

#endif // PDFMCPTASKS_H
