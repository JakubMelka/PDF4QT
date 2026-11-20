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

#include "pdfmeasurescalerepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"

#include <QCoreApplication>
#include <QDateTime>

#include <cmath>

namespace pdf
{

/// The rows of the scales
class PDFMeasureScaleRepositoryHelper
{
public:
    PDFMeasureScaleRepositoryHelper() = delete;

    /// Longest id of a unit, which is read (the ids of PDFMeasureUnits are short)
    static constexpr int MAXIMUM_UNIT_LENGTH = 16;

    static PDFDatabaseError invalidScale()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFMeasureScaleRepository", "Invalid scale."));
    }

    /// A real column holds a finite number (an integer or a real, never a text, a blob or NULL)
    static std::optional<double> readNumber(const PDFDatabaseStatement& statement, int column)
    {
        const QVariant value = statement.getVariant(column);
        const int type = value.typeId();
        if (type != QMetaType::Double && type != QMetaType::LongLong && type != QMetaType::Int)
        {
            return std::nullopt;
        }
        const double number = value.toDouble();
        return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
    }

    /// The scale of the columns name, paper_value, paper_unit, real_value, real_unit from \p column; std::nullopt
    /// for a damaged or hostile row (a value, which is no positive finite number, an unknown unit, a long name)
    static std::optional<PDFMeasureScale> readScale(const PDFDatabaseStatement& statement, int column)
    {
        const QVariant nameValue = statement.getVariant(column);
        if (!nameValue.isNull() && nameValue.typeId() != QMetaType::QString)
        {
            return std::nullopt;
        }
        const QString name = nameValue.toString();
        if (name.size() > PDFMeasureScaleRepository::MAXIMUM_NAME_LENGTH)
        {
            return std::nullopt;
        }

        const std::optional<double> paperValue = readNumber(statement, column + 1);
        const std::optional<double> realValue = readNumber(statement, column + 3);
        const QString paperUnit = PDFRepositoryHelper::readText(statement, column + 2, MAXIMUM_UNIT_LENGTH);
        const QString realUnit = PDFRepositoryHelper::readText(statement, column + 4, MAXIMUM_UNIT_LENGTH);
        if (!paperValue || !realValue)
        {
            return std::nullopt;
        }

        PDFMeasureScale scale(*paperValue, paperUnit.toLatin1(), *realValue, realUnit.toLatin1(), PDFRepositoryHelper::removeInvisibleCharacters(name, true, PDFMeasureScaleRepository::MAXIMUM_NAME_LENGTH));
        return scale.isValid() ? std::optional<PDFMeasureScale>(scale) : std::nullopt;
    }

    /// The scale of readScale with the description of the column after the real unit (a preset, the default
    /// scale); std::nullopt also for a description, which is no text or longer than MAXIMUM_DESCRIPTION_LENGTH
    static std::optional<PDFMeasureScale> readDescribedScale(const PDFDatabaseStatement& statement, int column)
    {
        std::optional<PDFMeasureScale> scale = readScale(statement, column);
        const QVariant descriptionValue = statement.getVariant(column + 5);
        if (!scale || (!descriptionValue.isNull() && descriptionValue.typeId() != QMetaType::QString))
        {
            return std::nullopt;
        }
        const QString description = descriptionValue.toString();
        if (description.size() > PDFMeasureScaleRepository::MAXIMUM_DESCRIPTION_LENGTH)
        {
            return std::nullopt;
        }
        scale->setDescription(PDFRepositoryHelper::removeInvisibleCharacters(description, true, PDFMeasureScaleRepository::MAXIMUM_DESCRIPTION_LENGTH));
        return scale;
    }

    /// Binds a scale to five parameters from \p index: name, paper value, paper unit, real value, real unit
    static void bindScale(PDFDatabaseStatement& statement, int index, const PDFMeasureScale& scale)
    {
        statement.bindText(index, scale.getName());
        statement.bindDouble(index + 1, scale.getPaperValue());
        statement.bindText(index + 2, QString::fromLatin1(scale.getPaperUnitId()));
        statement.bindDouble(index + 3, scale.getRealValue());
        statement.bindText(index + 4, QString::fromLatin1(scale.getRealUnitId()));
    }

    /// Binds a scale to six parameters from \p index: the five of bindScale and the description
    static void bindDescribedScale(PDFDatabaseStatement& statement, int index, const PDFMeasureScale& scale)
    {
        bindScale(statement, index, scale);
        statement.bindText(index + 5, scale.getDescription());
    }

    /// A scale, which can be written (valid, its name and its description not too long)
    static bool isWritable(const PDFMeasureScale& scale)
    {
        return scale.isValid() && scale.getName().size() <= PDFMeasureScaleRepository::MAXIMUM_NAME_LENGTH &&
               scale.getDescription().size() <= PDFMeasureScaleRepository::MAXIMUM_DESCRIPTION_LENGTH;
    }

    /// Runs one statement inside of an open transaction
    static PDFDatabaseError execute(PDFDatabaseConnection* connection, const char* sql)
    {
        PDFDatabaseStatement statement(connection, sql);
        if (!statement.execute())
        {
            return statement.getError();
        }
        return PDFDatabaseError();
    }
};

PDFMeasureScaleRepository::PDFMeasureScaleRepository(PDFDatabase* database) :
    m_database(database)
{

}

qint64 PDFMeasureScaleRepository::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

PDFDatabaseConnection* PDFMeasureScaleRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

std::vector<PDFMeasureScale> PDFMeasureScaleRepository::getPresets() const
{
    if (!arePresetsEdited())
    {
        return PDFMeasureScale::getDefaultPresets();
    }

    std::vector<PDFMeasureScale> presets;
    PDFDatabaseStatement statement(getConnection(), "SELECT name, paper_value, paper_unit, real_value, real_unit, description FROM measure_scale_preset ORDER BY position, id LIMIT ?1");
    statement.bindInt64(1, MAXIMUM_PRESETS);
    while (statement.next())
    {
        if (std::optional<PDFMeasureScale> scale = PDFMeasureScaleRepositoryHelper::readDescribedScale(statement, 0))
        {
            presets.push_back(std::move(*scale));
        }
    }
    return presets;
}

bool PDFMeasureScaleRepository::arePresetsEdited() const
{
    PDFDatabaseStatement statement(getConnection(), "SELECT presets_edited FROM measure_scale_state WHERE id = 1");
    bool isEdited = false;
    if (statement.next())
    {
        isEdited = PDFRepositoryHelper::readInteger(statement, 0, 0, 0, 1) != 0;
        statement.reset();
    }
    return isEdited;
}

PDFDatabaseError PDFMeasureScaleRepository::setPresets(const std::vector<PDFMeasureScale>& presets)
{
    using Helper = PDFMeasureScaleRepositoryHelper;

    if (presets.size() > size_t(MAXIMUM_PRESETS))
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFMeasureScaleRepository", "At most %1 presets can be stored.").arg(MAXIMUM_PRESETS));
    }
    for (const PDFMeasureScale& preset : presets)
    {
        if (!Helper::isWritable(preset))
        {
            return Helper::invalidScale();
        }
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseError error = Helper::execute(connection, "DELETE FROM measure_scale_preset");
    if (error.isError())
    {
        return error;
    }

    for (size_t i = 0; i < presets.size(); ++i)
    {
        PDFDatabaseStatement statement(connection, "INSERT INTO measure_scale_preset(position, name, paper_value, paper_unit, real_value, real_unit, description) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
        statement.bindInt64(1, qint64(i));
        Helper::bindDescribedScale(statement, 2, presets[i]);
        if (!statement.execute())
        {
            return statement.getError();
        }
    }

    // The user edited the list: an empty table is an empty list from now on
    error = Helper::execute(connection, "INSERT INTO measure_scale_state(id, presets_edited) VALUES (1, 1) "
                                        "ON CONFLICT(id) DO UPDATE SET presets_edited = 1 WHERE measure_scale_state.presets_edited IS NOT 1");
    if (error.isError())
    {
        return error;
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMeasureScaleRepository::restoreDefaultPresets()
{
    using Helper = PDFMeasureScaleRepositoryHelper;

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseError error = Helper::execute(connection, "DELETE FROM measure_scale_preset");
    if (!error.isError())
    {
        error = Helper::execute(connection, "UPDATE measure_scale_state SET presets_edited = 0 WHERE id = 1 AND presets_edited IS NOT 0");
    }
    if (error.isError())
    {
        return error;
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFMeasureScale PDFMeasureScaleRepository::getDefaultScale() const
{
    PDFDatabaseStatement statement(getConnection(), "SELECT default_name, default_paper_value, default_paper_unit, default_real_value, default_real_unit, default_description FROM measure_scale_state WHERE id = 1");
    std::optional<PDFMeasureScale> scale;
    if (statement.next())
    {
        scale = PDFMeasureScaleRepositoryHelper::readDescribedScale(statement, 0);
        statement.reset();
    }

    // 473:92070: without a stored scale the real size (the first built-in preset, "Real size (1:1)")
    return scale ? *scale : PDFMeasureScale::getDefaultPresets().front();
}

PDFDatabaseError PDFMeasureScaleRepository::setDefaultScale(const PDFMeasureScale& scale)
{
    if (!PDFMeasureScaleRepositoryHelper::isWritable(scale))
    {
        return PDFMeasureScaleRepositoryHelper::invalidScale();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO measure_scale_state(id, default_name, default_paper_value, default_paper_unit, default_real_value, default_real_unit, default_description) "
                                               "VALUES (1, ?1, ?2, ?3, ?4, ?5, ?6) ON CONFLICT(id) DO UPDATE SET default_name = excluded.default_name, "
                                               "default_paper_value = excluded.default_paper_value, default_paper_unit = excluded.default_paper_unit, "
                                               "default_real_value = excluded.default_real_value, default_real_unit = excluded.default_real_unit, "
                                               "default_description = excluded.default_description");
    PDFMeasureScaleRepositoryHelper::bindDescribedScale(statement, 1, scale);
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

bool PDFMeasureScaleRepository::isDocumentIdentifiable(const QString& path, const QByteArray& documentId)
{
    // ISO 32000-2, 14.4: the first identifier of /ID is permanent; a document without a file or /ID cannot be
    // remembered (473:89712)
    return PDFRepositoryHelper::isValidStoredPath(path) && !documentId.isEmpty() && documentId.size() <= MAXIMUM_DOCUMENT_ID_LENGTH;
}

std::optional<PDFMeasureScale> PDFMeasureScaleRepository::findDocumentScale(const QString& path, const QByteArray& documentId) const
{
    if (!isDocumentIdentifiable(path, documentId))
    {
        return std::nullopt;
    }

    PDFDatabaseStatement statement(getConnection(), "SELECT name, paper_value, paper_unit, real_value, real_unit FROM measure_document_scale WHERE path = ?1 AND document_id = ?2");
    statement.bindText(1, path);
    statement.bindBlob(2, documentId);
    std::optional<PDFMeasureScale> scale;
    if (statement.next())
    {
        scale = PDFMeasureScaleRepositoryHelper::readScale(statement, 0);
        statement.reset();
    }
    return scale;
}

PDFDatabaseError PDFMeasureScaleRepository::setDocumentScale(const QString& path, const QByteArray& documentId, const PDFMeasureScale& scale)
{
    if (!isDocumentIdentifiable(path, documentId) || !PDFMeasureScaleRepositoryHelper::isWritable(scale))
    {
        return PDFMeasureScaleRepositoryHelper::invalidScale();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO measure_document_scale(path, document_id, name, paper_value, paper_unit, real_value, real_unit, last_used) "
                                               "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) ON CONFLICT(path, document_id) DO UPDATE SET name = excluded.name, "
                                               "paper_value = excluded.paper_value, paper_unit = excluded.paper_unit, real_value = excluded.real_value, "
                                               "real_unit = excluded.real_unit, last_used = excluded.last_used");
    statement.bindText(1, path);
    statement.bindBlob(2, documentId);
    PDFMeasureScaleRepositoryHelper::bindScale(statement, 3, scale);
    statement.bindInt64(8, getCurrentTime());
    if (!statement.execute())
    {
        return statement.getError();
    }

    // Plan 4.8: at most MAXIMUM_DOCUMENT_SCALES documents, the least recently used ones go
    PDFDatabaseStatement prune(connection, "DELETE FROM measure_document_scale WHERE rowid NOT IN "
                                           "(SELECT rowid FROM measure_document_scale ORDER BY last_used DESC, rowid DESC LIMIT ?1)");
    prune.bindInt64(1, MAXIMUM_DOCUMENT_SCALES);
    if (!prune.execute())
    {
        return prune.getError();
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFMeasureScaleRepository::markDocumentScaleUsed(const QString& path, const QByteArray& documentId)
{
    if (!isDocumentIdentifiable(path, documentId))
    {
        return PDFMeasureScaleRepositoryHelper::invalidScale();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // The same time is not written again (the change log gets no change without a difference)
    PDFDatabaseStatement statement(connection, "UPDATE measure_document_scale SET last_used = ?3 WHERE path = ?1 AND document_id = ?2 AND last_used IS NOT ?3");
    statement.bindText(1, path);
    statement.bindBlob(2, documentId);
    statement.bindInt64(3, getCurrentTime());
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

PDFDatabaseError PDFMeasureScaleRepository::removeDocumentScale(const QString& path, const QByteArray& documentId)
{
    if (!isDocumentIdentifiable(path, documentId))
    {
        return PDFMeasureScaleRepositoryHelper::invalidScale();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    PDFDatabaseStatement statement(connection, "DELETE FROM measure_document_scale WHERE path = ?1 AND document_id = ?2");
    statement.bindText(1, path);
    statement.bindBlob(2, documentId);
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
