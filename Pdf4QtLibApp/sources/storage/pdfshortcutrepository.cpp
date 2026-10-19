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

#include "pdfshortcutrepository.h"
#include "pdfdatabasestatement.h"

#include <QCoreApplication>

namespace pdf
{

PDFShortcutRepository::PDFShortcutRepository(PDFDatabase* database) :
    m_database(database)
{

}

std::map<QString, QString> PDFShortcutRepository::getShortcuts(PDFDatabaseError* error) const
{
    std::map<QString, QString> shortcuts;
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseStatement statement(connection, "SELECT command_id, keys FROM shortcut");
    while (statement.next())
    {
        const QString commandId = statement.getText(0);
        const QString keys = statement.getText(1);
        if (!commandId.isEmpty() && keys.size() <= MAXIMUM_KEYS_LENGTH)
        {
            shortcuts[commandId] = keys;
        }
    }

    if (error)
    {
        *error = statement.getError();
    }
    return shortcuts;
}

PDFDatabaseError PDFShortcutRepository::setShortcut(const QString& commandId, const QString& keys)
{
    if (commandId.isEmpty() || keys.size() > MAXIMUM_KEYS_LENGTH)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFShortcutRepository", "Invalid shortcut."));
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO shortcut(command_id, keys) VALUES (?1, ?2) "
                                               "ON CONFLICT(command_id) DO UPDATE SET keys = excluded.keys WHERE shortcut.keys IS NOT excluded.keys");
    statement.bindText(1, commandId);
    statement.bindText(2, keys);
    if (!statement.execute())
    {
        return statement.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFShortcutRepository::resetShortcut(const QString& commandId)
{
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM shortcut WHERE command_id = ?1");
    statement.bindText(1, commandId);
    if (!statement.execute())
    {
        return statement.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFShortcutRepository::resetAll()
{
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM shortcut");
    if (!statement.execute())
    {
        return statement.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

}   // namespace pdf
