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

#ifndef PDFAPPMCPACTIVITY_H
#define PDFAPPMCPACTIVITY_H

#include "pdfappglobal.h"
#include "pdfmcprepository.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <map>
#include <set>
#include <vector>

class QTimer;

namespace pdf
{

class PDFDatabase;

/// Texts of the activity rows of the AI page (plan 2.3, Figma 653:5231 /
/// 955:150808): the title of an action id with its counts (%Ln), the meta line
/// and the status line. The rows store the action id and a detail; the texts
/// are made at display in the language of the user interface.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpActivity
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppMcpActivity)

public:
    PDFAppMcpActivity() = delete;

    /// Title of the row ("Read text", "Add 3 highlights", "Save a copy"); the
    /// detail gives "count" and "kind" of the changes
    static QString getTitle(const QString& action, const QJsonObject& detail);

    /// Meta line "Claude Code · Project_Report.pdf · pages 1–12"
    static QString getMetaText(const QString& client, const QString& document, const QJsonObject& detail);

    /// Status line ("Allowed by you", "Denied · the document is locked",
    /// "Failed · <reason>"); empty for a successful row without a question.
    /// \p isOtherInstance: a waiting row of another PDF4QT window.
    static QString getStatusText(PDFMcpActivityStatus status, const QString& client, const QJsonObject& detail, bool isOtherInstance = false);

    /// The detail "reason" of a row denied by the bound of the queue of the
    /// confirmations (DeniedTimeout of a request, whose dialog never appeared
    /// within 10 minutes - item 59)
    static QString getQueueTimeoutReason() { return QStringLiteral("queue"); }

    /// Pages "1–12, 15" of page numbers
    static QString formatPages(const std::set<int>& pages);

    /// Page numbers of a detail "pages" text (the inverse of formatPages; at most 10 000)
    static std::set<int> parsePages(const QString& text);

    /// The action reads document data (coalesced, plan 3.0); application.get_state is not logged
    static bool isReadAction(const QString& action);
    static bool isLoggedAction(const QString& action);
};

/// One new activity row
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpActivityEntry
{
public:
    QString assistantKey;           ///< Key of the coalescing (never stored)
    QString client;                 ///< Display name
    QString action;                 ///< Action id
    QString document;               ///< Title of the document
    QString documentId;             ///< Key of the coalescing (never stored)
    std::set<int> pages;            ///< Pages read (coalesced into "pages")
    QJsonObject detail;
    PDFMcpActivityStatus status = PDFMcpActivityStatus::Ok;
};

/// Writer of the activity (plan 6): rows are batched every 250 ms into one
/// BEGIN IMMEDIATE transaction; reads of the same assistant / document /
/// action within 60 s are coalesced into one row with the pages merged, so are
/// the requests refused by a Deny permission (with the count of the attempts).
/// The status of a row (waiting -> allowed / denied_*) is changed by its local
/// id, also before the row is written. Rows of a refused write stay for the
/// next flush (tried again after RETRY_INTERVAL; at most MAXIMUM_UNWRITTEN_ROWS,
/// the oldest finished reads dropped first, a waiting row never). GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpActivityLog : public QObject
{
    Q_OBJECT

public:
    explicit PDFAppMcpActivityLog(PDFDatabase* database, QObject* parent = nullptr);
    virtual ~PDFAppMcpActivityLog() override;

    /// Interval of the batches and the window of the coalescing (ms)
    static constexpr int FLUSH_INTERVAL = 250;
    static constexpr qint64 COALESCE_TIME = 60000;

    /// A written row is kept for the changes of its status this long after its
    /// last change (a confirmation waits up to 2 min, the job of the change runs
    /// after it - its final status must reach the row); at most MAXIMUM_KEPT_ROWS
    static constexpr qint64 KEEP_TIME = 15 * 60000;
    static constexpr size_t MAXIMUM_KEPT_ROWS = 4096;

    /// A refused write is tried again after this interval (ms)
    static constexpr int RETRY_INTERVAL = 5000;

    /// Rows kept for the next flush after refused writes (waiting rows are never
    /// dropped) - the table keeps no more rows than this
    static constexpr size_t MAXIMUM_UNWRITTEN_ROWS = size_t(PDFMcpRepository::MAXIMUM_ACTIVITY_ROWS);

    /// Bound of the attempts counted in a coalesced row of a denial
    static constexpr int MAXIMUM_ATTEMPTS = 1000000;

    /// Fixed time (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// Records a row; returns its local id (> 0), 0 for an action, which is not logged
    qint64 record(const PDFAppMcpActivityEntry& entry);

    /// Changes the status (and merges the detail) of a row
    void update(qint64 localId, PDFMcpActivityStatus status, const QJsonObject& detail = QJsonObject());

    /// Writes the pending rows now (also by the destructor)
    void flush();

    /// Rows waiting for the write (tests)
    int getPendingCount() const;

Q_SIGNALS:
    /// Rows were written
    void written();

private:
    class Row
    {
    public:
        PDFAppMcpActivityEntry entry;
        qint64 time = 0;
        qint64 lastChange = 0;      ///< Time of the last record / update (the row is kept KEEP_TIME after it)
        qint64 databaseId = 0;      ///< 0 = not written yet
        bool isDirty = true;
    };

    qint64 getCurrentTime() const;
    void scheduleFlush();

    /// Forgets the written rows, which cannot change any more (KEEP_TIME, MAXIMUM_KEPT_ROWS)
    void forgetWrittenRows(qint64 now);

    /// Drops the oldest finished unwritten rows above MAXIMUM_UNWRITTEN_ROWS
    void limitUnwrittenRows();

    QPointer<PDFDatabase> m_database;
    QPointer<QTimer> m_timer;
    std::map<qint64, Row> m_rows;           ///< Rows by their local ids (written rows are kept for 60 s of coalescing / status changes)
    qint64 m_nextId = 1;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFAPPMCPACTIVITY_H
