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

#include "pdfdatabasestatement.h"

#include <QCoreApplication>

#include <sqlite3.h>

#include <atomic>
#include <limits>

namespace pdf
{

PDFDatabaseStatement::PDFDatabaseStatement(PDFDatabaseConnection* connection, const char* sql)
{
    if (!connection || !connection->isValid())
    {
        m_error = connection && connection->getOpenError().isError() ? connection->getOpenError()
                                                                     : PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The database is not open."));
        return;
    }

    m_database = connection->getDatabase();
    m_handle = connection->getHandle();
    const int result = sqlite3_prepare_v2(m_handle, sql, -1, &m_statement, nullptr);
    if (result != SQLITE_OK)
    {
        setError(result);
        sqlite3_finalize(m_statement);
        m_statement = nullptr;
    }
}

PDFDatabaseStatement::~PDFDatabaseStatement()
{
    // Finalize also resets the statement and releases its read transaction
    sqlite3_finalize(m_statement);
    m_statement = nullptr;
}

void PDFDatabaseStatement::setError(int code)
{
    if (!m_error.isError())
    {
        m_error = PDFDatabaseError::fromHandle(m_handle, code);

        // A damaged database requests the integrity check at the next opening
        if (m_database)
        {
            m_database->reportError(m_error);
        }
    }
}

void PDFDatabaseStatement::bindNull(int index)
{
    if (m_statement)
    {
        const int result = sqlite3_bind_null(m_statement, index);
        if (result != SQLITE_OK)
        {
            setError(result);
        }
    }
}

void PDFDatabaseStatement::bindInt64(int index, qint64 value)
{
    if (m_statement)
    {
        const int result = sqlite3_bind_int64(m_statement, index, value);
        if (result != SQLITE_OK)
        {
            setError(result);
        }
    }
}

void PDFDatabaseStatement::bindDouble(int index, double value)
{
    if (m_statement)
    {
        const int result = sqlite3_bind_double(m_statement, index, value);
        if (result != SQLITE_OK)
        {
            setError(result);
        }
    }
}

void PDFDatabaseStatement::bindText(int index, const QString& text)
{
    if (m_statement)
    {
        const QByteArray data = text.toUtf8();
        const int result = sqlite3_bind_text64(m_statement, index, data.constData(), sqlite3_uint64(data.size()), SQLITE_TRANSIENT, SQLITE_UTF8);
        if (result != SQLITE_OK)
        {
            setError(result);
        }
    }
}

void PDFDatabaseStatement::bindBlob(int index, const QByteArray& data)
{
    if (m_statement)
    {
        // An empty blob is a blob of zero length, not a null
        const int result = data.isEmpty() ? sqlite3_bind_zeroblob(m_statement, index, 0)
                                          : sqlite3_bind_blob64(m_statement, index, data.constData(), sqlite3_uint64(data.size()), SQLITE_TRANSIENT);
        if (result != SQLITE_OK)
        {
            setError(result);
        }
    }
}

void PDFDatabaseStatement::bindVariant(int index, const QVariant& value)
{
    if (!value.isValid() || value.isNull())
    {
        bindNull(index);
        return;
    }

    switch (value.typeId())
    {
        case QMetaType::Bool:
            bindBool(index, value.toBool());
            break;

        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::Long:
        case QMetaType::LongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Char:
        case QMetaType::SChar:
        case QMetaType::UChar:
            bindInt64(index, value.toLongLong());
            break;

        case QMetaType::ULong:
        case QMetaType::ULongLong:
        {
            const qulonglong unsignedValue = value.toULongLong();
            if (unsignedValue > qulonglong(std::numeric_limits<qint64>::max()))
            {
                // Does not fit into an integer of SQLite
                bindDouble(index, double(unsignedValue));
            }
            else
            {
                bindInt64(index, qint64(unsignedValue));
            }
            break;
        }

        case QMetaType::Float:
        case QMetaType::Double:
            bindDouble(index, value.toDouble());
            break;

        case QMetaType::QByteArray:
            bindBlob(index, value.toByteArray());
            break;

        default:
            bindText(index, value.toString());
            break;
    }
}

bool PDFDatabaseStatement::next()
{
    m_hasRow = false;
    if (!m_statement || m_error.isError())
    {
        if (m_statement)
        {
            sqlite3_reset(m_statement);
        }
        return false;
    }

    const int result = sqlite3_step(m_statement);
    if (result == SQLITE_ROW)
    {
        m_hasRow = true;
        return true;
    }

    if (result != SQLITE_DONE)
    {
        setError(result);
    }

    // Read to the end (or failed): the read transaction is released at once
    sqlite3_reset(m_statement);
    return false;
}

bool PDFDatabaseStatement::execute()
{
    while (next())
    {
        // Rows of a statement run by execute are ignored
    }
    return isValid() && !m_error.isError();
}

void PDFDatabaseStatement::reset()
{
    m_hasRow = false;
    if (m_statement)
    {
        sqlite3_reset(m_statement);
    }
}

int PDFDatabaseStatement::getColumnCount() const
{
    return m_statement ? sqlite3_column_count(m_statement) : 0;
}

bool PDFDatabaseStatement::isColumnValid(int column) const
{
    return m_statement && m_hasRow && column >= 0 && column < sqlite3_column_count(m_statement);
}

bool PDFDatabaseStatement::isNull(int column) const
{
    return !isColumnValid(column) || sqlite3_column_type(m_statement, column) == SQLITE_NULL;
}

qint64 PDFDatabaseStatement::getInt64(int column) const
{
    return isColumnValid(column) ? sqlite3_column_int64(m_statement, column) : 0;
}

double PDFDatabaseStatement::getDouble(int column) const
{
    return isColumnValid(column) ? sqlite3_column_double(m_statement, column) : 0.0;
}

QString PDFDatabaseStatement::getText(int column) const
{
    if (!isColumnValid(column))
    {
        return QString();
    }

    const unsigned char* data = sqlite3_column_text(m_statement, column);
    const int size = sqlite3_column_bytes(m_statement, column);
    if (!data || size <= 0)
    {
        return QString();
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(data), size);
}

QByteArray PDFDatabaseStatement::getBlob(int column) const
{
    if (!isColumnValid(column))
    {
        return QByteArray();
    }

    const void* data = sqlite3_column_blob(m_statement, column);
    const int size = sqlite3_column_bytes(m_statement, column);
    if (!data || size <= 0)
    {
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<const char*>(data), size);
}

QVariant PDFDatabaseStatement::getVariant(int column) const
{
    if (!isColumnValid(column))
    {
        return QVariant();
    }

    switch (sqlite3_column_type(m_statement, column))
    {
        case SQLITE_INTEGER:
            return QVariant(qint64(sqlite3_column_int64(m_statement, column)));
        case SQLITE_FLOAT:
            return QVariant(sqlite3_column_double(m_statement, column));
        case SQLITE_TEXT:
            return QVariant(getText(column));
        case SQLITE_BLOB:
            return QVariant(getBlob(column));
        default:
            return QVariant();
    }
}

PDFDatabaseTransaction::PDFDatabaseTransaction(PDFDatabaseConnection* connection, Mode mode) :
    m_mode(mode)
{
    if (!connection || !connection->isValid())
    {
        m_error = connection && connection->getOpenError().isError() ? connection->getOpenError()
                                                                     : PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The database is not open."));
        return;
    }

    m_database = connection->getDatabase();
    m_handle = connection->getHandle();
    m_totalChangesAtBegin = sqlite3_total_changes64(m_handle);

    int result = SQLITE_OK;
    if (sqlite3_get_autocommit(m_handle) == 0)
    {
        // Nested transaction = savepoint of the outer transaction
        static std::atomic<quint64> savepointCounter = 0;
        m_savepoint = "pdf4qt_savepoint_" + QByteArray::number(++savepointCounter);
        result = sqlite3_exec(m_handle, ("SAVEPOINT " + m_savepoint).constData(), nullptr, nullptr, nullptr);
    }
    else
    {
        switch (mode)
        {
            case Mode::Read:
                result = executeWithRetry("BEGIN DEFERRED");
                break;
            case Mode::Write:
                result = executeWithRetry("BEGIN IMMEDIATE");
                break;
            case Mode::Exclusive:
                result = executeWithRetry("BEGIN EXCLUSIVE");
                break;
        }
    }

    if (result != SQLITE_OK)
    {
        setError(result);
        return;
    }

    m_isActive = true;
}

void PDFDatabaseTransaction::setError(int code)
{
    m_error = PDFDatabaseError::fromHandle(m_handle, code);
    if (m_database)
    {
        m_database->reportError(m_error);
    }
}

PDFDatabaseTransaction::~PDFDatabaseTransaction()
{
    rollback();
}

int PDFDatabaseTransaction::executeWithRetry(const char* sql)
{
    // The busy handler has waited for the busy timeout already, the operation is
    // tried once more (newapp_storage.md 2), then the error is reported
    int result = sqlite3_exec(m_handle, sql, nullptr, nullptr, nullptr);
    if ((result & 0xFF) == SQLITE_BUSY)
    {
        result = sqlite3_exec(m_handle, sql, nullptr, nullptr, nullptr);
    }
    return result;
}

bool PDFDatabaseTransaction::commit()
{
    if (!m_isActive)
    {
        if (!m_error.isError())
        {
            m_error = PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFDatabase", "The transaction is not active."));
        }
        return false;
    }

    int result = SQLITE_OK;
    if (!m_savepoint.isEmpty())
    {
        result = sqlite3_exec(m_handle, ("RELEASE " + m_savepoint).constData(), nullptr, nullptr, nullptr);
    }
    else
    {
        result = executeWithRetry("COMMIT");
    }

    if (result != SQLITE_OK)
    {
        setError(result);
        rollback();
        return false;
    }

    m_isActive = false;

    // Own changes do not change PRAGMA data_version of the own connection: the
    // notifier is woken up by the commit itself. A savepoint is committed by the
    // outer transaction (also a read transaction, in which a savepoint has written).
    const bool hasChanges = m_mode != Mode::Read || sqlite3_total_changes64(m_handle) != m_totalChangesAtBegin;
    if (m_savepoint.isEmpty() && hasChanges && m_database)
    {
        m_database->notifyCommitted();
    }

    return true;
}

void PDFDatabaseTransaction::rollback()
{
    if (!m_isActive)
    {
        return;
    }

    m_isActive = false;
    if (!m_savepoint.isEmpty())
    {
        sqlite3_exec(m_handle, ("ROLLBACK TO " + m_savepoint + "; RELEASE " + m_savepoint).constData(), nullptr, nullptr, nullptr);
    }
    else if (sqlite3_get_autocommit(m_handle) == 0)
    {
        // A failed statement may have rolled the transaction back already
        sqlite3_exec(m_handle, "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

}   // namespace pdf
