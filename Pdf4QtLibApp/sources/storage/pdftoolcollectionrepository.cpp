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

#include "pdftoolcollectionrepository.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"
#include "pdfappbuiltintoolcollections.h"

#include "pdfdocumenttoolcatalog.h"

#include <QCoreApplication>
#include <QDateTime>

#include <algorithm>
#include <limits>

namespace pdf
{

/// Helpers of the repository of the collections of tools
class PDFToolCollectionRepositoryHelper
{
public:
    PDFToolCollectionRepositoryHelper() = delete;

    /// Columns of a collection row: id, builtin_id (text or NULL), builtin_id IS
    /// NULL, name (text, bounded), icon (text or NULL), modified
    static constexpr const char* COLUMNS =
        "SELECT id, CASE WHEN typeof(builtin_id) = 'text' AND length(builtin_id) <= 64 THEN builtin_id END, builtin_id IS NULL, "
        "CASE WHEN typeof(name) = 'text' THEN substr(name, 1, 1024) END, CASE WHEN typeof(icon) = 'text' AND length(icon) <= 64 THEN icon END, "
        "modified FROM tool_collection ";

    /// Item of a collection read from the database
    class Item
    {
    public:
        QString toolId;
        qint64 position = 0;
        bool hasPosition = false;
        int catalogIndex = 0;
    };

    static bool isItemBefore(const Item& left, const Item& right)
    {
        if (left.hasPosition != right.hasPosition)
        {
            return left.hasPosition;
        }
        if (left.position != right.position)
        {
            return left.position < right.position;
        }
        return left.catalogIndex < right.catalogIndex;
    }

    /// Order of the stored collections: the overrides in the order of the built-in
    /// table, then the custom collections by their id
    static bool isRowBefore(const PDFToolCollectionRow& left, const PDFToolCollectionRow& right)
    {
        if (left.isBuiltin() != right.isBuiltin())
        {
            return left.isBuiltin();
        }
        if (left.isBuiltin())
        {
            return PDFAppBuiltinToolCollections::getIndex(left.builtinId) < PDFAppBuiltinToolCollections::getIndex(right.builtinId);
        }
        return left.id < right.id;
    }

    static PDFDatabaseError invalidName()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "The name of the collection is not valid."));
    }

    static PDFDatabaseError duplicateName(const QString& name)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "A collection named “%1” already exists.").arg(name));
    }

    static PDFDatabaseError notFound()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "The collection does not exist."));
    }

    static PDFDatabaseError changedElsewhere()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "The collection was changed in another window."));
    }

    static PDFDatabaseError unknownTool()
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "Unknown tool."));
    }

    /// Name of a built-in collection, which the user has not renamed
    static bool isBuiltinName(const PDFAppBuiltinToolCollection& collection, const QString& name)
    {
        return name == collection.getSourceName() || name == collection.getName();
    }

    /// Shown name of a built-in collection (its override renames it)
    static QString getShownName(const PDFAppBuiltinToolCollection& collection, const std::optional<PDFToolCollectionRow>& override)
    {
        return override && !override->name.isEmpty() ? override->name : collection.getName();
    }

    /// Key of the comparison of the names: names, which look the same, have the
    /// same key (review RS6) - the compatibility forms of NFKC (a no-break or another
    /// space, full-width letters, decomposed accents), runs of white space and the
    /// joiners, which a shown name keeps for the emoji sequences, never make two
    /// names different
    static QString getComparisonKey(const QString& name)
    {
        QString key = name.normalized(QString::NormalizationForm_KC);
        key.remove(QChar(0x200C));
        key.remove(QChar(0x200D));
        return key.simplified();
    }

    static bool isSameName(const QString& left, const QString& right)
    {
        return !left.isEmpty() && QString::compare(getComparisonKey(left), getComparisonKey(right), Qt::CaseInsensitive) == 0;
    }

    /// The collection has an item, which is not a tool of this catalog - a tool of a
    /// newer version (the same schema, the same file) or a value of another type.
    /// readItems leaves such items out and writeItems keeps them, so an override with
    /// them is never equal to its built-in definition and is never deleted as one
    /// (O25). An error of the query counts as such an item (the override is kept).
    static bool hasForeignItems(PDFDatabaseConnection* connection, qint64 collectionId)
    {
        const std::vector<PDFDocumentToolCatalogEntry>& tools = PDFDocumentToolCatalog::getTools();
        QByteArray sql("SELECT EXISTS (SELECT 1 FROM tool_collection_item WHERE collection_id = ?1 AND (tool_id IS NULL");
        if (!tools.empty())
        {
            sql += " OR tool_id NOT IN (";
            for (size_t i = 0; i < tools.size(); ++i)
            {
                sql += (i > 0 ? ", ?" : "?") + QByteArray::number(qulonglong(i + 2));
            }
            sql += ")";
        }
        else
        {
            sql += " OR 1";
        }
        sql += "))";

        PDFDatabaseStatement statement(connection, sql.constData());
        statement.bindInt64(1, collectionId);
        for (size_t i = 0; i < tools.size(); ++i)
        {
            statement.bindText(int(i + 2), tools[i].getId());
        }
        if (!statement.next())
        {
            return true;
        }
        const bool hasItems = statement.getInt64(0) != 0;
        statement.reset();
        return hasItems;
    }
};

PDFToolCollectionRepository::PDFToolCollectionRepository(PDFDatabase* database) :
    m_database(database)
{

}

qint64 PDFToolCollectionRepository::getCurrentTime() const
{
    return m_currentTime > 0 ? m_currentTime : QDateTime::currentMSecsSinceEpoch();
}

PDFDatabaseConnection* PDFToolCollectionRepository::getConnection() const
{
    return m_database ? m_database->getConnection() : nullptr;
}

QString PDFToolCollectionRepository::sanitizeName(const QString& name)
{
    return PDFRepositoryHelper::sanitizeStoredName(PDFRepositoryHelper::removeInvisibleCharacters(name.left(PDFRepositoryHelper::MAXIMUM_NAME_LENGTH * 4), false, PDFRepositoryHelper::MAXIMUM_NAME_LENGTH * 4));
}

QString PDFToolCollectionRepository::normalizeName(const QString& name)
{
    // A too long name is refused, not shortened
    if (name.size() > PDFRepositoryHelper::MAXIMUM_NAME_LENGTH * 16)
    {
        return QString();
    }
    return PDFRepositoryHelper::normalizeName(PDFRepositoryHelper::removeInvisibleCharacters(name, false, std::numeric_limits<qsizetype>::max()));
}

std::vector<PDFToolCollectionRow> PDFToolCollectionRepository::readRows(const char* condition, const QVariant& parameter, bool isItemsRead) const
{
    std::vector<PDFToolCollectionRow> rows;
    PDFDatabaseConnection* connection = getConnection();
    {
        const QByteArray sql = QByteArray(PDFToolCollectionRepositoryHelper::COLUMNS) + condition + " ORDER BY id LIMIT ?2";
        PDFDatabaseStatement statement(connection, sql.constData());
        if (parameter.isValid())
        {
            statement.bindVariant(1, parameter);
        }
        else
        {
            statement.bindNull(1);
        }
        statement.bindInt64(2, MAXIMUM_READ_COLLECTIONS);

        while (statement.next())
        {
            PDFToolCollectionRow row;
            row.id = PDFRepositoryHelper::readInteger(statement, 0, 0, 1, std::numeric_limits<qint64>::max());
            if (row.id <= 0)
            {
                continue;
            }

            const bool isCustom = PDFRepositoryHelper::readInteger(statement, 2, 0, 0, 1) != 0;
            if (!isCustom)
            {
                // An override of an unknown built-in collection (another version) is skipped
                row.builtinId = PDFRepositoryHelper::readText(statement, 1, PDFAppBuiltinToolCollections::MAXIMUM_ID_LENGTH);
                if (!PDFAppBuiltinToolCollections::findCollection(row.builtinId))
                {
                    continue;
                }
            }

            row.name = sanitizeName(PDFRepositoryHelper::readText(statement, 3, 1024));
            if (row.isBuiltin())
            {
                // The English name or the translated name is "not renamed"
                const PDFAppBuiltinToolCollection* collection = PDFAppBuiltinToolCollections::findCollection(row.builtinId);
                if (collection && PDFToolCollectionRepositoryHelper::isBuiltinName(*collection, row.name))
                {
                    row.name.clear();
                }
            }
            else if (row.name.isEmpty())
            {
                row.name = QCoreApplication::translate("pdf::PDFToolCollectionRepository", "Untitled collection");
            }

            const QString iconName = PDFRepositoryHelper::readText(statement, 4, 64);
            row.iconName = PDFAppBuiltinToolCollections::isValidIconName(iconName) ? iconName : QString();
            row.modified = PDFRepositoryHelper::readInteger(statement, 5, 0, 0, std::numeric_limits<qint64>::max());
            rows.push_back(row);
        }
    }

    if (isItemsRead)
    {
        for (PDFToolCollectionRow& row : rows)
        {
            row.toolIds = readItems(connection, row.id);
        }
    }

    std::stable_sort(rows.begin(), rows.end(), PDFToolCollectionRepositoryHelper::isRowBefore);
    return rows;
}

QStringList PDFToolCollectionRepository::readItems(PDFDatabaseConnection* connection, qint64 id) const
{
    using Item = PDFToolCollectionRepositoryHelper::Item;

    std::vector<Item> items;
    PDFDatabaseStatement statement(connection, "SELECT CASE WHEN typeof(tool_id) = 'text' AND length(tool_id) <= 64 THEN tool_id END, "
                                               "CASE WHEN typeof(position) = 'integer' THEN position END "
                                               "FROM tool_collection_item WHERE collection_id = ?1 ORDER BY position LIMIT ?2");
    statement.bindInt64(1, id);
    statement.bindInt64(2, MAXIMUM_READ_ITEMS);
    while (statement.next())
    {
        Item item;
        item.toolId = PDFRepositoryHelper::readText(statement, 0, PDFDocumentToolCatalog::MAXIMUM_ID_LENGTH);
        item.catalogIndex = PDFDocumentToolCatalog::getToolIndex(item.toolId);
        if (item.catalogIndex < 0)
        {
            continue;
        }
        item.hasPosition = !statement.isNull(1);
        item.position = PDFRepositoryHelper::readInteger(statement, 1, 0, std::numeric_limits<qint64>::min(), std::numeric_limits<qint64>::max());
        items.push_back(item);
    }

    std::stable_sort(items.begin(), items.end(), PDFToolCollectionRepositoryHelper::isItemBefore);

    QStringList toolIds;
    for (const Item& item : items)
    {
        if (!toolIds.contains(item.toolId))
        {
            toolIds << item.toolId;
        }
    }
    return toolIds;
}

std::vector<PDFToolCollectionRow> PDFToolCollectionRepository::getCollections() const
{
    return readRows("WHERE ?1 IS NULL", QVariant());
}

std::optional<PDFToolCollectionRow> PDFToolCollectionRepository::getCollection(qint64 id) const
{
    if (id <= 0)
    {
        return std::nullopt;
    }

    std::vector<PDFToolCollectionRow> rows = readRows("WHERE id = ?1", QVariant(id));
    if (rows.empty())
    {
        return std::nullopt;
    }
    return rows.front();
}

std::optional<PDFToolCollectionRow> PDFToolCollectionRepository::getBuiltinOverride(const QString& builtinId) const
{
    if (!PDFAppBuiltinToolCollections::findCollection(builtinId))
    {
        return std::nullopt;
    }

    std::vector<PDFToolCollectionRow> rows = readRows("WHERE builtin_id = ?1", QVariant(builtinId));
    if (rows.empty())
    {
        return std::nullopt;
    }
    return rows.front();
}

int PDFToolCollectionRepository::getCustomCollectionCount() const
{
    PDFDatabaseStatement statement(getConnection(), "SELECT COUNT(*) FROM tool_collection WHERE builtin_id IS NULL");
    int count = 0;
    if (statement.next())
    {
        count = int(PDFRepositoryHelper::readInteger(statement, 0, 0, 0, std::numeric_limits<int>::max()));
        statement.reset();
    }
    return count;
}

std::optional<PDFToolCollectionRow> PDFToolCollectionRepository::findByName(const QString& name, qint64 excludedId, const QString& excludedBuiltinId) const
{
    using Helper = PDFToolCollectionRepositoryHelper;

    const QString normalizedName = normalizeName(name);
    if (normalizedName.isEmpty())
    {
        return std::nullopt;
    }

    // The names only (Save runs this inside of its write transaction); the tools of
    // the found collection are read at the end
    std::vector<PDFToolCollectionRow> rows = readRows("WHERE ?1 IS NULL", QVariant(), false);
    for (PDFToolCollectionRow& row : rows)
    {
        if (!row.isBuiltin() && row.id != excludedId && Helper::isSameName(row.name, normalizedName))
        {
            row.toolIds = readItems(getConnection(), row.id);
            return row;
        }
    }

    for (const PDFAppBuiltinToolCollection& collection : PDFAppBuiltinToolCollections::getCollections())
    {
        if (collection.getId() == excludedBuiltinId)
        {
            continue;
        }

        std::optional<PDFToolCollectionRow> override;
        for (const PDFToolCollectionRow& row : rows)
        {
            if (row.builtinId == collection.getId())
            {
                override = row;
            }
        }

        const QString shownName = Helper::getShownName(collection, override);
        if (Helper::isSameName(collection.getSourceName(), normalizedName) || Helper::isSameName(collection.getName(), normalizedName) ||
            Helper::isSameName(shownName, normalizedName))
        {
            PDFToolCollectionRow result;
            result.id = override ? override->id : 0;
            result.builtinId = collection.getId();
            result.name = shownName;
            result.iconName = override && !override->iconName.isEmpty() ? override->iconName : collection.getIconName();
            result.modified = override ? override->modified : 0;
            result.toolIds = override ? readItems(getConnection(), override->id) : collection.getToolIds();
            return result;
        }
    }

    return std::nullopt;
}

PDFDatabaseError PDFToolCollectionRepository::validateTools(const QStringList& toolIds)
{
    if (toolIds.isEmpty() || toolIds.size() > MAXIMUM_READ_ITEMS)
    {
        return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "A collection needs at least one tool."));
    }

    for (qsizetype i = 0; i < toolIds.size(); ++i)
    {
        if (!PDFDocumentToolCatalog::isValidToolId(toolIds[i]) || toolIds.indexOf(toolIds[i]) != i)
        {
            return PDFToolCollectionRepositoryHelper::unknownTool();
        }
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFToolCollectionRepository::writeItems(PDFDatabaseConnection* connection, qint64 id, const QStringList& toolIds)
{
    // Only the items of the tools of this catalog are replaced: an item of a tool,
    // which a newer version has added (the same schema, the same file), stays (O25).
    // One statement with a parameter per tool of the catalog (review RC6-2: not a
    // statement per tool); the catalog is far below the parameter limit of SQLite.
    const std::vector<PDFDocumentToolCatalogEntry>& tools = PDFDocumentToolCatalog::getTools();
    if (!tools.empty())
    {
        QByteArray sql("DELETE FROM tool_collection_item WHERE collection_id = ?1 AND tool_id IN (");
        for (size_t i = 0; i < tools.size(); ++i)
        {
            sql += (i > 0 ? ", ?" : "?") + QByteArray::number(qulonglong(i + 2));
        }
        sql += ")";

        PDFDatabaseStatement statement(connection, sql.constData());
        statement.bindInt64(1, id);
        for (size_t i = 0; i < tools.size(); ++i)
        {
            statement.bindText(int(i + 2), tools[i].getId());
        }
        if (!statement.execute())
        {
            return statement.getError();
        }
    }

    PDFDatabaseStatement statement(connection, "INSERT INTO tool_collection_item(collection_id, tool_id, position) VALUES (?1, ?2, ?3)");
    for (qsizetype i = 0; i < toolIds.size(); ++i)
    {
        statement.bindInt64(1, id);
        statement.bindText(2, toolIds[i]);
        statement.bindInt64(3, qint64(i));
        if (!statement.execute())
        {
            return statement.getError();
        }
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFToolCollectionRepository::saveCollection(const PDFToolCollectionDraft& draft, qint64* id, bool* isDuplicate, bool* isLimit, bool* isChanged)
{
    using Helper = PDFToolCollectionRepositoryHelper;

    if (isChanged)
    {
        *isChanged = false;
    }
    if (id)
    {
        *id = 0;
    }
    if (isDuplicate)
    {
        *isDuplicate = false;
    }
    if (isLimit)
    {
        *isLimit = false;
    }

    const QString normalizedName = normalizeName(draft.name);
    if (normalizedName.isEmpty())
    {
        return Helper::invalidName();
    }

    PDFDatabaseError error = validateTools(draft.toolIds);
    if (error.isError())
    {
        return error;
    }

    const PDFAppBuiltinToolCollection* builtin = nullptr;
    if (!draft.builtinId.isEmpty())
    {
        builtin = PDFAppBuiltinToolCollections::findCollection(draft.builtinId);
        if (!builtin)
        {
            return Helper::notFound();
        }
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    // The duplicate check is inside of the write transaction (another instance)
    if (std::optional<PDFToolCollectionRow> existing = findByName(normalizedName, builtin ? 0 : draft.id, draft.builtinId))
    {
        if (isDuplicate)
        {
            *isDuplicate = true;
        }
        return Helper::duplicateName(existing->name);
    }

    const qint64 now = getCurrentTime();
    qint64 collectionId = 0;
    const QString iconName = PDFAppBuiltinToolCollections::isValidIconName(draft.iconName) ? draft.iconName : QString();

    if (builtin)
    {
        const bool isRenamed = !Helper::isBuiltinName(*builtin, normalizedName);
        const bool isDefaultIcon = iconName.isEmpty() || iconName == builtin->getIconName();
        const std::optional<PDFToolCollectionRow> override = getBuiltinOverride(builtin->getId());

        if (!isRenamed && isDefaultIcon && draft.toolIds == builtin->getToolIds() &&
            !(override && Helper::hasForeignItems(connection, override->id)))
        {
            // The built-in definition: the override is not needed (Reset to default +
            // Save); an override with the tools of a newer version stays (O25)
            for (const char* sql : { "DELETE FROM tool_collection_item WHERE collection_id IN (SELECT id FROM tool_collection WHERE builtin_id = ?1)",
                                     "DELETE FROM tool_collection WHERE builtin_id = ?1" })
            {
                PDFDatabaseStatement statement(connection, sql);
                statement.bindText(1, builtin->getId());
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

        PDFDatabaseStatement statement(connection, override ? "UPDATE tool_collection SET name = ?2, icon = ?3, modified = ?4 WHERE builtin_id = ?1"
                                                            : "INSERT INTO tool_collection(builtin_id, name, icon, modified) VALUES (?1, ?2, ?3, ?4)");
        statement.bindText(1, builtin->getId());
        statement.bindText(2, isRenamed ? normalizedName : builtin->getSourceName());
        if (isDefaultIcon)
        {
            statement.bindNull(3);
        }
        else
        {
            statement.bindText(3, iconName);
        }
        statement.bindInt64(4, now);
        if (!statement.execute())
        {
            return statement.getError();
        }
        collectionId = override ? override->id : connection->getLastInsertRowId();
    }
    else if (draft.id <= 0)
    {
        if (getCustomCollectionCount() >= MAXIMUM_CUSTOM_COLLECTIONS)
        {
            if (isLimit)
            {
                *isLimit = true;
            }
            return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "You can have at most %1 collections. Delete a collection to create a new one.").arg(MAXIMUM_CUSTOM_COLLECTIONS));
        }

        PDFDatabaseStatement statement(connection, "INSERT INTO tool_collection(builtin_id, name, icon, modified) VALUES (NULL, ?1, ?2, ?3)");
        statement.bindText(1, normalizedName);
        if (iconName.isEmpty())
        {
            statement.bindNull(2);
        }
        else
        {
            statement.bindText(2, iconName);
        }
        statement.bindInt64(3, now);
        if (!statement.execute())
        {
            return statement.getError();
        }
        collectionId = connection->getLastInsertRowId();
    }
    else
    {
        // The collection the editor has seen (as deleteCustomCollection): the id of a
        // collection deleted by another instance may denote its new collection now
        if (draft.expectedModified)
        {
            const std::optional<PDFToolCollectionRow> row = getCollection(draft.id);
            if (row && !row->isBuiltin() && row->modified != *draft.expectedModified)
            {
                if (isChanged)
                {
                    *isChanged = true;
                }
                return Helper::changedElsewhere();
            }
        }

        PDFDatabaseStatement statement(connection, "UPDATE tool_collection SET name = ?2, icon = ?3, modified = ?4 WHERE id = ?1 AND builtin_id IS NULL");
        statement.bindInt64(1, draft.id);
        statement.bindText(2, normalizedName);
        if (iconName.isEmpty())
        {
            statement.bindNull(3);
        }
        else
        {
            statement.bindText(3, iconName);
        }
        statement.bindInt64(4, now);
        if (!statement.execute())
        {
            return statement.getError();
        }

        if (connection->getChanges() == 0)
        {
            return Helper::notFound();
        }
        collectionId = draft.id;
    }

    error = writeItems(connection, collectionId, draft.toolIds);
    if (error.isError())
    {
        return error;
    }

    if (!transaction.commit())
    {
        return transaction.getError();
    }

    if (id)
    {
        *id = collectionId;
    }
    return PDFDatabaseError();
}

PDFDatabaseError PDFToolCollectionRepository::createCollection(const QString& name, const QString& iconName, const QStringList& toolIds,
                                                               qint64* id, bool* isDuplicate, bool* isLimit)
{
    PDFToolCollectionDraft draft;
    draft.name = name;
    draft.iconName = iconName;
    draft.toolIds = toolIds;
    return saveCollection(draft, id, isDuplicate, isLimit);
}

PDFDatabaseError PDFToolCollectionRepository::deleteCollection(qint64 id)
{
    return deleteCustomCollection(id, nullptr, 0);
}

PDFDatabaseError PDFToolCollectionRepository::deleteCollection(qint64 id, const QString& expectedName, qint64 expectedModified)
{
    return deleteCustomCollection(id, &expectedName, expectedModified);
}

PDFDatabaseError PDFToolCollectionRepository::deleteCustomCollection(qint64 id, const QString* expectedName, qint64 expectedModified)
{
    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    {
        PDFDatabaseStatement exists(connection, "SELECT builtin_id IS NULL FROM tool_collection WHERE id = ?1");
        exists.bindInt64(1, id);
        const bool isFound = exists.next();
        const bool isCustom = isFound && exists.getInt64(0) != 0;
        exists.reset();
        if (!isFound)
        {
            return PDFToolCollectionRepositoryHelper::notFound();
        }
        if (!isCustom)
        {
            return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "A built-in collection cannot be deleted."));
        }
    }

    // The confirmed collection: the id of a collection deleted by another instance
    // may denote its new collection now (review RS6)
    if (expectedName)
    {
        const std::optional<PDFToolCollectionRow> row = getCollection(id);
        if (!row || row->name != *expectedName || row->modified != expectedModified)
        {
            return PDFToolCollectionRepositoryHelper::changedElsewhere();
        }
    }

    // Items first: the cascade of the foreign key works only with foreign_keys=ON
    for (const char* sql : { "DELETE FROM tool_collection_item WHERE collection_id = ?1", "DELETE FROM tool_collection WHERE id = ?1 AND builtin_id IS NULL" })
    {
        PDFDatabaseStatement statement(connection, sql);
        statement.bindInt64(1, id);
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

PDFDatabaseError PDFToolCollectionRepository::resetBuiltin(const QString& builtinId)
{
    if (!PDFAppBuiltinToolCollections::findCollection(builtinId))
    {
        return PDFToolCollectionRepositoryHelper::notFound();
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    for (const char* sql : { "DELETE FROM tool_collection_item WHERE collection_id IN (SELECT id FROM tool_collection WHERE builtin_id = ?1)",
                             "DELETE FROM tool_collection WHERE builtin_id = ?1" })
    {
        PDFDatabaseStatement statement(connection, sql);
        statement.bindText(1, builtinId);
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

PDFDatabaseError PDFToolCollectionRepository::setToolIncluded(qint64 collectionId, const QString& builtinId, const QString& toolId, bool isIncluded)
{
    using Helper = PDFToolCollectionRepositoryHelper;

    if (!PDFDocumentToolCatalog::isValidToolId(toolId))
    {
        return Helper::unknownTool();
    }

    const PDFAppBuiltinToolCollection* builtin = nullptr;
    if (!builtinId.isEmpty())
    {
        builtin = PDFAppBuiltinToolCollections::findCollection(builtinId);
        if (!builtin)
        {
            return Helper::notFound();
        }
    }

    PDFDatabaseConnection* connection = getConnection();
    PDFDatabaseTransaction transaction(connection);
    if (!transaction.isActive())
    {
        return transaction.getError();
    }

    std::optional<PDFToolCollectionRow> row = builtin ? getBuiltinOverride(builtinId) : getCollection(collectionId);
    if (!builtin && (!row || row->isBuiltin()))
    {
        return Helper::notFound();
    }

    QStringList toolIds = row ? row->toolIds : builtin->getToolIds();
    if (toolIds.contains(toolId) == isIncluded)
    {
        return PDFDatabaseError();
    }

    if (isIncluded)
    {
        if (toolIds.size() >= MAXIMUM_READ_ITEMS)
        {
            return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "The collection has too many tools."));
        }
        toolIds << toolId;
    }
    else
    {
        if (toolIds.size() <= 1)
        {
            return PDFDatabaseError::fromMessage(QCoreApplication::translate("pdf::PDFToolCollectionRepository", "A collection needs at least one tool."));
        }
        toolIds.removeAll(toolId);
    }

    if (builtin && row)
    {
        // The change gives the built-in definition again (not renamed, the default
        // icon, its tools in their order, no tool of a newer version - O25): the
        // override is not needed, as by Save
        const bool isRenamed = !row->name.isEmpty() && !Helper::isBuiltinName(*builtin, row->name);
        const bool isDefaultIcon = row->iconName.isEmpty() || row->iconName == builtin->getIconName();
        if (!isRenamed && isDefaultIcon && toolIds == builtin->getToolIds() && !Helper::hasForeignItems(connection, row->id))
        {
            for (const char* sql : { "DELETE FROM tool_collection_item WHERE collection_id IN (SELECT id FROM tool_collection WHERE builtin_id = ?1)",
                                     "DELETE FROM tool_collection WHERE builtin_id = ?1" })
            {
                PDFDatabaseStatement statement(connection, sql);
                statement.bindText(1, builtin->getId());
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
    }

    qint64 id = row ? row->id : 0;
    if (row)
    {
        PDFDatabaseStatement statement(connection, "UPDATE tool_collection SET modified = ?2 WHERE id = ?1");
        statement.bindInt64(1, id);
        statement.bindInt64(2, getCurrentTime());
        if (!statement.execute())
        {
            return statement.getError();
        }
    }
    else
    {
        // A built-in collection without an override gets one (not renamed, default icon)
        PDFDatabaseStatement statement(connection, "INSERT INTO tool_collection(builtin_id, name, icon, modified) VALUES (?1, ?2, NULL, ?3)");
        statement.bindText(1, builtin->getId());
        statement.bindText(2, builtin->getSourceName());
        statement.bindInt64(3, getCurrentTime());
        if (!statement.execute())
        {
            return statement.getError();
        }
        id = connection->getLastInsertRowId();
    }

    PDFDatabaseError error = writeItems(connection, id, toolIds);
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

PDFDatabaseError PDFToolCollectionRepository::addTool(qint64 collectionId, const QString& builtinId, const QString& toolId)
{
    return setToolIncluded(collectionId, builtinId, toolId, true);
}

}   // namespace pdf
