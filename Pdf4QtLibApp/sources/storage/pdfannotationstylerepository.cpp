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

#include "pdfannotationstylerepository.h"
#include "pdfdatabasestatement.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonParseError>

namespace pdf
{

/// Conversion of the stored values
class PDFAnnotationStyleRepositoryHelper
{
public:
    PDFAnnotationStyleRepositoryHelper() = delete;

    /// JSON object of a stored value (text or blob of a sane size), nullopt otherwise
    static std::optional<QJsonObject> parse(const QVariant& value)
    {
        QByteArray data;
        if (value.typeId() == QMetaType::QString)
        {
            const QString text = value.toString();
            if (text.size() > PDFAnnotationStyleRepository::MAXIMUM_VALUE_SIZE)
            {
                return std::nullopt;
            }
            data = text.toUtf8();
        }
        else if (value.typeId() == QMetaType::QByteArray)
        {
            data = value.toByteArray();
        }
        else
        {
            return std::nullopt;
        }

        if (data.isEmpty() || data.size() > PDFAnnotationStyleRepository::MAXIMUM_VALUE_SIZE)
        {
            return std::nullopt;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
        {
            return std::nullopt;
        }
        return document.object();
    }

    static PDFDatabaseError invalidStyle()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFAnnotationStyleRepository", "Invalid annotation style."));
    }
};

PDFAnnotationStyleRepository::PDFAnnotationStyleRepository(PDFDatabase* database) :
    m_database(database)
{

}

bool PDFAnnotationStyleRepository::isValidStyleId(const QString& styleId)
{
    if (styleId.isEmpty() || styleId.size() > MAXIMUM_STYLE_ID_LENGTH)
    {
        return false;
    }

    for (const QChar character : styleId)
    {
        const char16_t code = character.unicode();
        if (code < 0x21 || code > 0x7E)
        {
            return false;
        }
    }
    return true;
}

std::optional<QJsonObject> PDFAnnotationStyleRepository::getValue(const QString& styleId, PDFDatabaseError* error) const
{
    if (error)
    {
        *error = PDFDatabaseError();
    }
    if (!isValidStyleId(styleId))
    {
        return std::nullopt;
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    // A hostile row of another program is not read into the memory: the size is
    // checked by SQLite (bytes of the value, text or blob), larger values are null
    PDFDatabaseStatement statement(connection, "SELECT CASE WHEN typeof(value) IN ('text', 'blob') AND length(CAST(value AS BLOB)) <= ?2 THEN value END "
                                               "FROM annotation_style WHERE style_id = ?1");
    statement.bindText(1, styleId);
    statement.bindInt64(2, MAXIMUM_VALUE_SIZE);

    std::optional<QJsonObject> result;
    if (statement.next())
    {
        result = PDFAnnotationStyleRepositoryHelper::parse(statement.getVariant(0));
        statement.reset();
    }

    if (error)
    {
        *error = statement.getError();
    }
    return result;
}

PDFDatabaseError PDFAnnotationStyleRepository::setValue(const QString& styleId, const QJsonObject& value)
{
    const QByteArray data = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (!isValidStyleId(styleId) || data.size() > MAXIMUM_VALUE_SIZE)
    {
        return PDFAnnotationStyleRepositoryHelper::invalidStyle();
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO annotation_style(style_id, value) VALUES (?1, ?2) "
                                               "ON CONFLICT(style_id) DO UPDATE SET value = excluded.value WHERE annotation_style.value IS NOT excluded.value");
    statement.bindText(1, styleId);
    statement.bindText(2, QString::fromUtf8(data));
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

PDFDatabaseError PDFAnnotationStyleRepository::removeValue(const QString& styleId)
{
    if (!isValidStyleId(styleId))
    {
        return PDFAnnotationStyleRepositoryHelper::invalidStyle();
    }

    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM annotation_style WHERE style_id = ?1");
    statement.bindText(1, styleId);
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

PDFDatabaseError PDFAnnotationStyleRepository::resetAll()
{
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM annotation_style");
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

QStringList PDFAnnotationStyleRepository::getStyleIds(PDFDatabaseError* error) const
{
    QStringList styleIds;
    PDFDatabaseConnection* connection = m_database ? m_database->getConnection() : nullptr;
    PDFDatabaseStatement statement(connection, "SELECT style_id FROM annotation_style ORDER BY style_id LIMIT ?1");
    statement.bindInt64(1, MAXIMUM_ROW_COUNT);
    while (statement.next())
    {
        const QString styleId = statement.getText(0);
        if (isValidStyleId(styleId))
        {
            styleIds << styleId;
        }
    }

    if (error)
    {
        *error = statement.getError();
    }
    return styleIds;
}

}   // namespace pdf
