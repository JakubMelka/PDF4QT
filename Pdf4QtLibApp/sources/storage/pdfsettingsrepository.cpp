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

#include "pdfsettingsrepository.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinition.h"

#include <QCoreApplication>
#include <QDateTime>

namespace pdf
{

PDFSettingsRepository::PDFSettingsRepository(PDFDatabase* database, const PDFSettingRegistry* registry) :
    m_database(database),
    m_registry(registry)
{

}

const PDFSettingDefinition* PDFSettingsRepository::getDefinition(const QString& key) const
{
    return m_registry ? m_registry->findDefinition(key) : nullptr;
}

QVariant PDFSettingsRepository::getStoredValue(const QString& key, PDFDatabaseError* error) const
{
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseStatement statement(connection, "SELECT value FROM setting WHERE key = ?1");
    statement.bindText(1, key);

    QVariant value;
    if (statement.next())
    {
        value = statement.getVariant(0);
        statement.reset();
    }

    if (error)
    {
        *error = statement.getError();
    }
    return value;
}

QVariant PDFSettingsRepository::getValue(const QString& key) const
{
    const PDFSettingDefinition* definition = getDefinition(key);
    if (!definition)
    {
        return QVariant();
    }

    return definition->validate(getStoredValue(key));
}

bool PDFSettingsRepository::getBool(const QString& key) const
{
    return getValue(key).toBool();
}

qint64 PDFSettingsRepository::getInteger(const QString& key) const
{
    return getValue(key).toLongLong();
}

double PDFSettingsRepository::getReal(const QString& key) const
{
    return getValue(key).toDouble();
}

QString PDFSettingsRepository::getString(const QString& key) const
{
    return getValue(key).toString();
}

bool PDFSettingsRepository::isChanged(const QString& key) const
{
    const PDFSettingDefinition* definition = getDefinition(key);
    if (!definition)
    {
        return false;
    }

    std::optional<QVariant> value = definition->normalize(getStoredValue(key));
    return value.has_value() && *value != definition->getDefaultValue();
}

QStringList PDFSettingsRepository::getChangedKeys(const QString& section) const
{
    QStringList keys;
    if (!m_registry)
    {
        return keys;
    }

    for (const PDFSettingDefinition* definition : m_registry->getSectionDefinitions(section))
    {
        if (isChanged(definition->getKey()))
        {
            keys << definition->getKey();
        }
    }
    return keys;
}

PDFDatabaseError PDFSettingsRepository::normalizeValue(const QString& key, const QVariant& value, QVariant* normalizedValue, bool* isDefault) const
{
    const PDFSettingDefinition* definition = getDefinition(key);
    if (!definition)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFSettingsRepository", "Unknown setting %1.").arg(key.left(128)));
    }

    std::optional<QVariant> normalized = definition->normalize(value);
    if (!normalized)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFSettingsRepository", "Invalid value of the setting %1.").arg(key));
    }

    *normalizedValue = *normalized;
    *isDefault = *normalized == definition->getDefaultValue();
    return PDFDatabaseError();
}

PDFDatabaseError PDFSettingsRepository::writeValue(PDFDatabaseConnection* connection, const QString& key, const QVariant& normalizedValue, bool isDefault)
{
    if (isDefault)
    {
        // Only changed settings are stored
        PDFDatabaseStatement statement(connection, "DELETE FROM setting WHERE key = ?1");
        statement.bindText(1, key);
        statement.execute();
        return statement.getError();
    }

    // The same value is not written again (no change notification without a change)
    PDFDatabaseStatement statement(connection, "INSERT INTO setting(key, value, modified) VALUES (?1, ?2, ?3) "
                                               "ON CONFLICT(key) DO UPDATE SET value = excluded.value, modified = excluded.modified "
                                               "WHERE setting.value IS NOT excluded.value");
    statement.bindText(1, key);
    statement.bindVariant(2, normalizedValue);
    statement.bindInt64(3, QDateTime::currentMSecsSinceEpoch());
    statement.execute();
    return statement.getError();
}

PDFDatabaseError PDFSettingsRepository::setValue(const QString& key, const QVariant& value)
{
    return setValues({ std::make_pair(key, value) });
}

PDFDatabaseError PDFSettingsRepository::setValues(const std::vector<std::pair<QString, QVariant>>& values)
{
    // Every value is validated before anything is written
    std::vector<std::pair<QVariant, bool>> normalizedValues;
    normalizedValues.reserve(values.size());
    for (const std::pair<QString, QVariant>& item : values)
    {
        QVariant normalizedValue;
        bool isDefault = false;
        const PDFDatabaseError error = normalizeValue(item.first, item.second, &normalizedValue, &isDefault);
        if (error.isError())
        {
            return error;
        }
        normalizedValues.emplace_back(std::move(normalizedValue), isDefault);
    }

    if (values.empty())
    {
        return PDFDatabaseError();
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    for (size_t i = 0; i < values.size(); ++i)
    {
        const PDFDatabaseError error = writeValue(connection, values[i].first, normalizedValues[i].first, normalizedValues[i].second);
        if (error.isError())
        {
            // The transaction is rolled back by its destructor: nothing is stored
            return error;
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }

    return PDFDatabaseError();
}

PDFDatabaseError PDFSettingsRepository::resetValue(const QString& key)
{
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM setting WHERE key = ?1");
    statement.bindText(1, key);
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

PDFDatabaseError PDFSettingsRepository::restoreDefaults(const QString& section)
{
    if (!m_registry || section.isEmpty())
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFSettingsRepository", "Unknown section of the settings."));
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM setting WHERE key = ?1");
    for (const PDFSettingDefinition* definition : m_registry->getSectionDefinitions(section))
    {
        statement.bindText(1, definition->getKey());
        if (!statement.execute())
        {
            return statement.getError();
        }
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }

    return PDFDatabaseError();
}

}   // namespace pdf
