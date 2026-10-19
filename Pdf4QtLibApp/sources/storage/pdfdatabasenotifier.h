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

#ifndef PDFDATABASENOTIFIER_H
#define PDFDATABASENOTIFIER_H

#include "pdfappglobal.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

namespace pdf
{

class PDFDatabase;

/// Notifications of the changes of the database (newapp_storage.md 3). Every table
/// has triggers writing the changed rows into change_log in the same transaction.
/// The notifier lives in the GUI thread and every 250 ms reads PRAGMA data_version
/// (changes of other connections - other processes and other threads) and the number
/// of changes of its own connection; when something has changed, it reads the new rows
/// of change_log and emits changed(topic, key) once per changed key. Own commits wake
/// it up at once (PDFDatabase::committed), so a page sees its own write without the
/// polling delay. A gap in change_log.seq (the log was pruned while this instance did
/// not read it) or too many rows emit everythingChanged() instead.
///
/// Topics are the names of the tables (PDFDatabaseSchema::getTables), the key is the
/// key of the changed row (setting key, path of a recent file, tool id, ...).
/// Slots may delete the notifier and run nested event loops.
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseNotifier : public QObject
{
    Q_OBJECT

public:
    /// Interval of the polling (newapp_storage.md 3)
    static constexpr int POLL_INTERVAL = 250;

    /// Interval of the pruning of the change log (once per hour)
    static constexpr int PRUNE_INTERVAL = 3600 * 1000;

    /// The pruning never waits for another instance, which writes: it is tried
    /// again after this interval
    static constexpr int PRUNE_RETRY_INTERVAL = 60 * 1000;

    /// Rows of the change log older than this are pruned (10 minutes)
    static constexpr qint64 LOG_MAXIMUM_AGE = 10 * 60 * 1000;

    /// At most this number of rows is kept in the change log
    static constexpr qint64 LOG_MAXIMUM_ROWS = 10000;

    /// More rows than this in one poll = everything has changed
    static constexpr int MAXIMUM_ROWS_PER_POLL = 1000;

    /// Creates the notifier of the database (in the thread of the database) and starts it
    explicit PDFDatabaseNotifier(PDFDatabase* database, QObject* parent = nullptr);
    virtual ~PDFDatabaseNotifier() override;

    /// Polling interval in milliseconds (tests); the polling is restarted
    void setPollInterval(int interval);

    /// Checks the database now (it is also called by the timer and by own commits)
    void poll();

    /// Prunes the change log (at start and every hour). It runs in the thread of the
    /// user interface, so it never waits for the lock of the database: when another
    /// instance (or another thread) writes, it gives up at once and it is tried again
    /// after PRUNE_RETRY_INTERVAL. \returns true, if the log was pruned.
    bool prune();

    /// Interval of the retry of a pruning, which met a lock (tests)
    void setPruneRetryInterval(int interval);

    /// A pruning, which met a lock, waits for its retry
    bool isPruneRetryPending() const { return m_pruneRetryTimer.isActive(); }

    /// The last processed sequence number of the change log
    qint64 getLastSequence() const { return m_lastSequence; }

Q_SIGNALS:
    /// Row \p key of the table \p topic has changed (in this or in another instance)
    void changed(const QString& topic, const QString& key);

    /// Unknown changes: reread everything
    void everythingChanged();

private:
    void onStateChanged();
    void resetBaseline();
    bool readCounters(qint64* dataVersion, qint64* totalChanges);

    QPointer<PDFDatabase> m_database;
    QTimer m_pollTimer;
    QTimer m_pruneTimer;
    QTimer m_pruneRetryTimer;
    qint64 m_lastSequence = 0;
    qint64 m_dataVersion = -1;
    qint64 m_totalChanges = -1;
    quint64 m_generation = 0;
    bool m_isPolling = false;
    bool m_pollAgain = false;
};

}   // namespace pdf

#endif // PDFDATABASENOTIFIER_H
