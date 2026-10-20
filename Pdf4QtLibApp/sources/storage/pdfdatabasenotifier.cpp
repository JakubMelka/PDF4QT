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

#include "pdfdatabasenotifier.h"
#include "pdfdatabase.h"
#include "pdfdatabasestatement.h"

#include <QDateTime>
#include <QSet>

#include <sqlite3.h>

namespace pdf
{

PDFDatabaseNotifier::PDFDatabaseNotifier(PDFDatabase* database, QObject* parent) :
    QObject(parent),
    m_database(database)
{
    m_pollTimer.setInterval(POLL_INTERVAL);
    m_pruneTimer.setInterval(PRUNE_INTERVAL);
    m_pruneRetryTimer.setInterval(PRUNE_RETRY_INTERVAL);
    m_pruneRetryTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &PDFDatabaseNotifier::poll);
    connect(&m_pruneTimer, &QTimer::timeout, this, &PDFDatabaseNotifier::prune);
    connect(&m_pruneRetryTimer, &QTimer::timeout, this, &PDFDatabaseNotifier::prune);

    if (database)
    {
        connect(database, &PDFDatabase::committed, this, &PDFDatabaseNotifier::poll);
        connect(database, &PDFDatabase::stateChanged, this, &PDFDatabaseNotifier::onStateChanged);
    }

    prune();
    resetBaseline();

    m_pollTimer.start();
    m_pruneTimer.start();
}

PDFDatabaseNotifier::~PDFDatabaseNotifier()
{
    m_pollTimer.stop();
    m_pruneTimer.stop();
    m_pruneRetryTimer.stop();
}

void PDFDatabaseNotifier::setPollInterval(int interval)
{
    m_pollTimer.start(qBound(1, interval, 3600 * 1000));
}

void PDFDatabaseNotifier::setPruneRetryInterval(int interval)
{
    m_pruneRetryTimer.setInterval(qBound(1, interval, PRUNE_INTERVAL));
    if (m_pruneRetryTimer.isActive())
    {
        m_pruneRetryTimer.start();
    }
}

bool PDFDatabaseNotifier::readCounters(qint64* dataVersion, qint64* totalChanges)
{
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    if (!connection || !connection->isValid())
    {
        return false;
    }

    PDFDatabaseError error;
    *dataVersion = connection->queryInteger("PRAGMA data_version", -1, &error);
    *totalChanges = sqlite3_total_changes64(connection->getHandle());
    return !error.isError();
}

void PDFDatabaseNotifier::resetBaseline()
{
    m_lastSequence = 0;
    m_dataVersion = -1;
    m_totalChanges = -1;
    m_generation = m_database ? m_database->getGeneration() : 0;

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    if (!connection || !connection->isValid())
    {
        return;
    }

    // The sequence of AUTOINCREMENT is the last number ever given, also when the
    // rows have been pruned
    m_lastSequence = connection->queryInteger("SELECT MAX(COALESCE((SELECT seq FROM sqlite_sequence WHERE name = 'change_log'), 0), "
                                              "COALESCE((SELECT MAX(seq) FROM change_log), 0))", 0);
    readCounters(&m_dataVersion, &m_totalChanges);
}

void PDFDatabaseNotifier::onStateChanged()
{
    // A new generation (open, Try Again) is checked by the next poll
    QMetaObject::invokeMethod(this, &PDFDatabaseNotifier::poll, Qt::QueuedConnection);
}

void PDFDatabaseNotifier::poll()
{
    if (!m_database)
    {
        return;
    }

    if (m_isPolling)
    {
        // Called from a slot of our own signal (nested event loop)
        m_pollAgain = true;
        return;
    }

    QPointer<PDFDatabaseNotifier> guard(this);
    m_isPolling = true;

    do
    {
        m_pollAgain = false;

        if (!m_database)
        {
            break;
        }

        if (m_database->getGeneration() != m_generation)
        {
            // Another database (Try Again, reopened): nothing is known about the changes
            resetBaseline();
            Q_EMIT everythingChanged();
            if (!guard)
            {
                return;
            }
            continue;
        }

        qint64 dataVersion = -1;
        qint64 totalChanges = -1;
        if (!readCounters(&dataVersion, &totalChanges))
        {
            break;
        }

        if (dataVersion == m_dataVersion && totalChanges == m_totalChanges)
        {
            break;
        }

        PDFDatabaseConnection* connection = m_database->getConnection();

        // One snapshot for the rows and the sequence (another process may commit
        // between the statements); it ends before the signals are emitted
        PDFDatabaseTransaction readTransaction(connection, PDFDatabaseTransaction::Mode::Read);
        PDFDatabaseStatement statement(connection, "SELECT seq, topic, key FROM change_log WHERE seq > ?1 ORDER BY seq LIMIT ?2");
        statement.bindInt64(1, m_lastSequence);
        statement.bindInt64(2, MAXIMUM_ROWS_PER_POLL + 1);

        std::vector<std::pair<QString, QString>> changes;
        QSet<std::pair<QString, QString>> seen;
        qint64 lastSequence = m_lastSequence;
        qint64 expectedSequence = m_lastSequence + 1;
        bool hasGap = false;
        int rowCount = 0;
        while (statement.next())
        {
            // Every hole is a gap, not only the one before the first row: the pruning
            // deletes the rows by their time, which follows the sequence only, while
            // the clock is not set back
            const qint64 sequence = statement.getInt64(0);
            if (sequence != expectedSequence)
            {
                hasGap = true;
            }
            expectedSequence = sequence + 1;
            lastSequence = qMax(lastSequence, sequence);
            ++rowCount;

            std::pair<QString, QString> change(statement.getText(1), statement.getText(2));
            if (!seen.contains(change))
            {
                seen.insert(change);
                changes.push_back(std::move(change));
            }
        }

        if (statement.getError().isError())
        {
            // Try again at the next poll
            break;
        }

        m_dataVersion = dataVersion;
        m_totalChanges = totalChanges;

        bool isEverythingChanged = false;
        if (rowCount == 0)
        {
            // No new rows, but the sequence has moved on: rows, which this instance has
            // not read, were pruned meanwhile (it was suspended, another instance pruned
            // the log at its start). A smaller sequence = the log was recreated.
            const qint64 maximum = connection->queryInteger("SELECT MAX(COALESCE((SELECT seq FROM sqlite_sequence WHERE name = 'change_log'), 0), "
                                                            "COALESCE((SELECT MAX(seq) FROM change_log), 0))", m_lastSequence);
            if (maximum != m_lastSequence)
            {
                isEverythingChanged = true;
                lastSequence = maximum;
            }
        }
        else if (hasGap || rowCount > MAXIMUM_ROWS_PER_POLL)
        {
            // Gap = rows were pruned before this instance has read them; too many
            // rows = it is cheaper to reread everything
            isEverythingChanged = true;
            if (rowCount > MAXIMUM_ROWS_PER_POLL)
            {
                lastSequence = connection->queryInteger("SELECT COALESCE(MAX(seq), 0) FROM change_log", lastSequence);
            }
        }

        m_lastSequence = lastSequence;
        statement.reset();
        if (readTransaction.isActive())
        {
            readTransaction.commit();
        }

        if (isEverythingChanged)
        {
            Q_EMIT everythingChanged();
            if (!guard)
            {
                return;
            }
        }
        else
        {
            for (const std::pair<QString, QString>& change : changes)
            {
                Q_EMIT changed(change.first, change.second);
                if (!guard)
                {
                    return;
                }
            }
        }

        // A change made by a slot is read at once
        m_pollAgain = true;
    }
    while (m_pollAgain);

    m_isPolling = false;
}

bool PDFDatabaseNotifier::prune()
{
    m_pruneRetryTimer.stop();

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    if (!connection || !connection->isValid())
    {
        return false;
    }

    if (connection->isInTransaction())
    {
        // Called inside of a transaction of the GUI thread: it would become a part of it
        m_pruneRetryTimer.start();
        return false;
    }

    // The GUI thread must not freeze, while another instance holds the lock of the
    // database (BEGIN IMMEDIATE would wait for the busy timeout twice): the pruning
    // does not wait at all and it is tried again later. Readers are not affected:
    // the sequence of the log continues (AUTOINCREMENT), so a notifier, which has
    // not read the pruned rows, finds a gap and reports that everything has changed.
    const int busyTimeout = connection->getBusyTimeout();
    connection->setBusyTimeout(0);

    bool isPruned = false;
    bool isBusy = false;
    {
        PDFDatabaseTransaction transaction(connection);
        if (!transaction.isActive())
        {
            isBusy = transaction.getError().isBusy();
        }
        else
        {
            PDFDatabaseStatement statement(connection, "DELETE FROM change_log WHERE time < ?1 OR seq <= (SELECT MAX(seq) FROM change_log) - ?2");
            statement.bindInt64(1, QDateTime::currentMSecsSinceEpoch() - LOG_MAXIMUM_AGE);
            statement.bindInt64(2, LOG_MAXIMUM_ROWS);
            if (!statement.execute())
            {
                isBusy = statement.getError().isBusy();
            }
            else if (!transaction.commit())
            {
                isBusy = transaction.getError().isBusy();
            }
            else
            {
                isPruned = true;
            }
        }
    }

    connection->setBusyTimeout(busyTimeout);

    if (isBusy)
    {
        m_pruneRetryTimer.start();
    }

    return isPruned;
}

}   // namespace pdf
