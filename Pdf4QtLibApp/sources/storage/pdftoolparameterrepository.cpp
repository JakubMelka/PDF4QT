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

#include "pdftoolparameterrepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"

#include "pdfdocumenttoolcatalog.h"

#include <QCoreApplication>

namespace pdf
{

/// Helpers of the repository of the parameters
class PDFToolParameterRepositoryHelper
{
public:
    PDFToolParameterRepositoryHelper() = delete;

    /// Longest stored text, which is read at all (longer texts are hostile)
    static constexpr int MAXIMUM_STORED_TEXT_LENGTH = 70000;

    static QString tr(const char* text)
    {
        return QCoreApplication::translate("pdf::PDFToolParameterRepository", text);
    }

    /// Error of a text marked by QT_TRANSLATE_NOOP("pdf::PDFToolParameterRepository", ...)
    static PDFDatabaseError error(const char* text)
    {
        return PDFDatabaseError::fromMessage(tr(text));
    }

    static PDFDatabaseError deleteValue(PDFDatabaseConnection* connection, const QString& toolId, const QString& name)
    {
        PDFDatabaseStatement statement(connection, "DELETE FROM tool_parameter WHERE tool_id = ?1 AND name = ?2");
        statement.bindText(1, toolId);
        statement.bindText(2, name);
        statement.execute();
        return statement.getError();
    }

    static PDFDatabaseError writeValue(PDFDatabaseConnection* connection, const QString& toolId, const QString& name, const QVariant& value)
    {
        // The same value is not written again (no notification without a change)
        PDFDatabaseStatement statement(connection, "INSERT INTO tool_parameter(tool_id, name, value) VALUES (?1, ?2, ?3) "
                                                   "ON CONFLICT(tool_id, name) DO UPDATE SET value = excluded.value "
                                                   "WHERE tool_parameter.value IS NOT excluded.value");
        statement.bindText(1, toolId);
        statement.bindText(2, name);
        statement.bindVariant(3, value);
        statement.execute();
        return statement.getError();
    }
};

PDFToolParameterRepository::PDFToolParameterRepository(PDFDatabase* database) :
    m_database(database)
{

}

PDFDatabaseConnection* PDFToolParameterRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

QVariantMap PDFToolParameterRepository::getValues(const QString& toolId, const PDFDocumentToolParameterDefinitions& definitions) const
{
    QVariantMap values = PDFDocumentToolParameters::getDefaultValues(definitions);
    if (!PDFDocumentToolCatalog::isValidToolId(toolId) || !PDFDocumentToolParameters::isValid(definitions))
    {
        return values;
    }

    PDFDatabaseStatement statement(getConnection(),
        "SELECT CASE WHEN typeof(name) = 'text' AND length(name) <= 64 THEN name END, "
        "CASE WHEN typeof(value) IN ('integer', 'real') OR (typeof(value) = 'text' AND length(value) <= ?3) THEN value END "
        "FROM tool_parameter WHERE tool_id = ?1 ORDER BY rowid LIMIT ?2");
    statement.bindText(1, toolId);
    statement.bindInt64(2, MAXIMUM_READ_ROWS);
    statement.bindInt64(3, PDFToolParameterRepositoryHelper::MAXIMUM_STORED_TEXT_LENGTH);
    while (statement.next())
    {
        const QString name = PDFRepositoryHelper::readText(statement, 0, PDFDocumentToolParameterDefinition::MAXIMUM_NAME_LENGTH);
        const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(definitions, name);
        if (!definition || !definition->isStored())
        {
            continue;
        }

        const std::optional<QVariant> value = definition->fromStorageValue(statement.getVariant(1));
        if (value)
        {
            values.insert(name, *value);
        }
    }
    return values;
}

PDFDatabaseError PDFToolParameterRepository::setValues(const QString& toolId, const PDFDocumentToolParameterDefinitions& definitions, const QVariantMap& values)
{
    using Helper = PDFToolParameterRepositoryHelper;

    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return Helper::error(QT_TRANSLATE_NOOP("pdf::PDFToolParameterRepository", "Unknown tool."));
    }
    if (!PDFDocumentToolParameters::isValid(definitions))
    {
        return Helper::error(QT_TRANSLATE_NOOP("pdf::PDFToolParameterRepository", "The parameters of the tool are not valid."));
    }

    // Everything is validated before anything is written
    QVariantMap storedValues;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
    {
        const PDFDocumentToolParameterDefinition* definition = PDFDocumentToolParameters::find(definitions, it.key());
        if (!definition)
        {
            return Helper::error(QT_TRANSLATE_NOOP("pdf::PDFToolParameterRepository", "Unknown parameter of the tool."));
        }
        if (!definition->isStored())
        {
            continue;
        }

        QVariant normalized;
        const QString message = definition->normalize(it.value(), &normalized);
        if (!message.isEmpty())
        {
            return PDFDatabaseError::fromMessage(message);
        }
        storedValues.insert(it.key(), normalized);
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    for (const PDFDocumentToolParameterDefinition& definition : definitions)
    {
        PDFDatabaseError error;
        if (!definition.isStored())
        {
            // A Secret is never kept, also not a row written by something else
            error = Helper::deleteValue(connection, toolId, definition.getName());
        }
        else if (storedValues.contains(definition.getName()))
        {
            const QVariant value = storedValues.value(definition.getName());
            if (!definition.isRequired() && value == definition.getDefaultValue())
            {
                error = Helper::deleteValue(connection, toolId, definition.getName());
            }
            else
            {
                error = Helper::writeValue(connection, toolId, definition.getName(), definition.toStorageValue(value));
            }
        }

        if (error.isError())
        {
            return error;
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFToolParameterRepository::setValue(const QString& toolId, const PDFDocumentToolParameterDefinition& definition, const QVariant& value)
{
    if (!definition.isStored())
    {
        return PDFToolParameterRepositoryHelper::error(QT_TRANSLATE_NOOP("pdf::PDFToolParameterRepository", "Secret values are never stored."));
    }
    return setValues(toolId, { definition }, { { definition.getName(), value } });
}

PDFDatabaseError PDFToolParameterRepository::clearValues(const QString& toolId)
{
    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return PDFToolParameterRepositoryHelper::error(QT_TRANSLATE_NOOP("pdf::PDFToolParameterRepository", "Unknown tool."));
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM tool_parameter WHERE tool_id = ?1");
    statement.bindText(1, toolId);
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
