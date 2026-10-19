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

#ifndef PDFDATABASESTATEMENT_H
#define PDFDATABASESTATEMENT_H

#include "pdfdatabase.h"

#include <QByteArray>
#include <QString>
#include <QVariant>

struct sqlite3;
struct sqlite3_stmt;

namespace pdf
{

/// Prepared statement (RAII). Parameters are bound by their index (from 1) and
/// their type, columns are read by their index (from 0). A read statement is reset
/// automatically, when its rows have been read (next returns false), so it does
/// not keep a read transaction open and does not block the checkpoint of the WAL
/// (newapp_storage.md 2). A statement, which is not read to its end, is reset by
/// reset() or by its destructor.
///
/// Example:
/// \code
/// PDFDatabaseStatement statement(database->getConnection(), "SELECT value FROM setting WHERE key = ?1");
/// statement.bindText(1, key);
/// if (statement.next()) { value = statement.getVariant(0); }
/// statement.reset();
/// \endcode
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseStatement
{
public:
    /// Prepares the statement on the connection (nullptr connection = invalid statement)
    PDFDatabaseStatement(PDFDatabaseConnection* connection, const char* sql);
    ~PDFDatabaseStatement();

    PDFDatabaseStatement(const PDFDatabaseStatement&) = delete;
    PDFDatabaseStatement& operator=(const PDFDatabaseStatement&) = delete;

    /// Statement has been prepared
    bool isValid() const { return m_statement != nullptr; }

    /// The first error of the preparation, of a binding or of a step
    const PDFDatabaseError& getError() const { return m_error; }

    void bindNull(int index);
    void bindInt64(int index, qint64 value);
    void bindBool(int index, bool value) { bindInt64(index, value ? 1 : 0); }
    void bindDouble(int index, double value);
    void bindText(int index, const QString& text);
    void bindBlob(int index, const QByteArray& data);

    /// Binds the value by its type: null, bool / integers, floating point,
    /// QByteArray (blob), anything else as text
    void bindVariant(int index, const QVariant& value);

    /// Steps to the next row. \returns true, if a row is available. False means
    /// the end of the rows or an error (getError); the statement is reset then.
    bool next();

    /// Runs a statement without a result to its end. \returns true on success.
    bool execute();

    /// Resets the statement (the bound parameters are kept)
    void reset();

    /// Number of columns of the result
    int getColumnCount() const;

    bool isNull(int column) const;
    qint64 getInt64(int column) const;
    bool getBool(int column) const { return getInt64(column) != 0; }
    double getDouble(int column) const;
    QString getText(int column) const;
    QByteArray getBlob(int column) const;

    /// Value of the column by its storage class: null = invalid QVariant, integer = qint64,
    /// real = double, text = QString, blob = QByteArray
    QVariant getVariant(int column) const;

private:
    bool isColumnValid(int column) const;
    void setError(int code);

    PDFDatabase* m_database = nullptr;
    sqlite3* m_handle = nullptr;
    sqlite3_stmt* m_statement = nullptr;
    PDFDatabaseError m_error;
    bool m_hasRow = false;
};

/// Transaction (RAII): it must be committed explicitly, otherwise it is rolled back
/// by the destructor. A write transaction is BEGIN IMMEDIATE (no deadlock by an upgrade
/// of a read lock); SQLITE_BUSY after the busy timeout is tried once more, then the
/// transaction reports the error (isActive is false) - the caller shows the error and
/// never throws. Keep write transactions short: never wait for the user, a dialog or
/// another thread inside of them. A transaction inside of another one is a savepoint.
class PDF4QTLIBAPPSHARED_EXPORT PDFDatabaseTransaction
{
public:
    enum class Mode
    {
        Read,       ///< BEGIN DEFERRED - consistent snapshot of more reads
        Write,      ///< BEGIN IMMEDIATE
        Exclusive   ///< BEGIN EXCLUSIVE
    };

    explicit PDFDatabaseTransaction(PDFDatabaseConnection* connection, Mode mode = Mode::Write);
    ~PDFDatabaseTransaction();

    PDFDatabaseTransaction(const PDFDatabaseTransaction&) = delete;
    PDFDatabaseTransaction& operator=(const PDFDatabaseTransaction&) = delete;

    /// Transaction has begun and it was neither committed nor rolled back
    bool isActive() const { return m_isActive; }

    /// Error of the begin or of the commit
    const PDFDatabaseError& getError() const { return m_error; }

    /// Commits the transaction. \returns true on success. A committed write transaction
    /// wakes up the notifier of the changes (PDFDatabase::notifyCommitted).
    bool commit();

    /// Rolls the transaction back
    void rollback();

private:
    int executeWithRetry(const char* sql);
    void setError(int code);

    PDFDatabase* m_database = nullptr;
    sqlite3* m_handle = nullptr;
    Mode m_mode = Mode::Write;
    QByteArray m_savepoint;
    qint64 m_totalChangesAtBegin = 0;
    PDFDatabaseError m_error;
    bool m_isActive = false;
};

}   // namespace pdf

#endif // PDFDATABASESTATEMENT_H
