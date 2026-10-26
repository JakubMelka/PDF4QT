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

#ifndef PDFTOOLCOLLECTIONREPOSITORY_H
#define PDFTOOLCOLLECTIONREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>
#include <vector>

namespace pdf
{

class PDFDatabaseConnection;

/// Stored collection of tools (tables tool_collection, tool_collection_item): a
/// custom collection (builtinId empty) or the override of a built-in collection,
/// which the user has changed (builtinId = PDFAppBuiltinToolCollection::getId).
/// Values read from the database are sanitized (see PDFToolCollectionRepository).
class PDF4QTLIBAPPSHARED_EXPORT PDFToolCollectionRow
{
public:
    qint64 id = 0;              ///< Row id (> 0)
    QString builtinId;          ///< Built-in collection of the override, empty = custom collection
    QString name;               ///< Custom: never empty ("Untitled collection"). Override: empty = not renamed (the translated name is shown)
    QString iconName;           ///< One of PDFAppBuiltinToolCollections::getIconChoices; empty = the default icon (NULL)
    qint64 modified = 0;        ///< Milliseconds since the epoch of the last change, 0 = unknown
    QStringList toolIds;        ///< Tools (valid identifiers of the catalog, unique) in their order

    bool isBuiltin() const { return !builtinId.isEmpty(); }
};

/// Collection, which is saved (Save of the collections editor, plan of step 6, 4.4)
class PDF4QTLIBAPPSHARED_EXPORT PDFToolCollectionDraft
{
public:
    qint64 id = 0;              ///< Custom collection to replace, 0 = a new custom collection (ignored for a built-in one)
    QString builtinId;          ///< Built-in collection (its override is written), empty = custom
    QString name;               ///< Name entered by the user
    QString iconName;           ///< Icon of getIconChoices; empty or another one = the default icon
    QStringList toolIds;        ///< Tools in their order (at least one; invalid / duplicate ids are refused)
};

/// Collections of tools (newapp_storage.md 4, plan of step 6 chapter 2). The
/// seven built-in collections are a static table (PDFAppBuiltinToolCollections):
/// a built-in collection has a row only after the user has changed it (its
/// override); the stored name equal to the English name means "not renamed" and
/// an icon NULL the default icon; Reset to default deletes the row. A built-in
/// collection cannot be deleted. Custom collections have builtin_id NULL.
///
/// Names are normalized (control and format characters - bidirectional controls,
/// zero width characters except the joiners - removed, white space trimmed, at
/// most PDFRepositoryHelper::MAXIMUM_NAME_LENGTH characters; the field of the
/// editor allows MAXIMUM_EDITED_NAME_LENGTH) and unique: compared without case
/// with every custom name and every name of a built-in collection (English,
/// translated and renamed). Every write is one BEGIN IMMEDIATE transaction with
/// the name check inside, so two instances are serialized and the second one
/// sees the duplicate. At most MAXIMUM_CUSTOM_COLLECTIONS custom collections.
///
/// Rows are untrusted: an unknown builtin_id is skipped, unknown and duplicate
/// tool ids are skipped, equal positions are ordered by the catalog, an icon
/// outside of the allowed icons is the default icon, a NULL / empty / overlong
/// name is sanitized and bounded, a non-numeric modified time is 0. A value
/// object: every read goes to the database, writes are notified with the topics
/// getCollectionTopic / getItemTopic (key = row id). Usable in any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFToolCollectionRepository
{
public:
    /// Most custom collections (the creation of another one is refused)
    static constexpr int MAXIMUM_CUSTOM_COLLECTIONS = 200;

    /// Most collections and most items of a collection, which are read
    static constexpr int MAXIMUM_READ_COLLECTIONS = 1000;
    static constexpr int MAXIMUM_READ_ITEMS = 1000;

    /// Longest name of the name field of the collections editor (283:1888)
    static constexpr int MAXIMUM_EDITED_NAME_LENGTH = 64;

    static QString getCollectionTopic() { return QStringLiteral("tool_collection"); }
    static QString getItemTopic() { return QStringLiteral("tool_collection_item"); }

    explicit PDFToolCollectionRepository(PDFDatabase* database);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// Stored collections: the overrides of the built-in collections (in the order
    /// of the built-in table) and the custom collections (by their id)
    std::vector<PDFToolCollectionRow> getCollections() const;

    /// Custom collection or override by its row id
    std::optional<PDFToolCollectionRow> getCollection(qint64 id) const;

    /// Override of the built-in collection, std::nullopt = not changed
    std::optional<PDFToolCollectionRow> getBuiltinOverride(const QString& builtinId) const;

    /// Number of the custom collections
    int getCustomCollectionCount() const;

    /// Collection using the name (without case): a custom collection (its row) or a
    /// built-in one (a row with builtinId, id of its override or 0, its shown name).
    /// \p excludedId and \p excludedBuiltinId are not compared (the edited collection).
    /// std::nullopt = the name is free (or empty after the normalization).
    std::optional<PDFToolCollectionRow> findByName(const QString& name, qint64 excludedId = 0, const QString& excludedBuiltinId = QString()) const;

    /// Normalized name, empty = invalid (empty or too long)
    static QString normalizeName(const QString& name);

    /// Name read from the database or shown: format and control characters removed,
    /// bounded (never refused)
    static QString sanitizeName(const QString& name);

    /// Saves the collection in one transaction: the name check, the collection row
    /// and the replacement of all its items. A new custom collection (draft.id 0)
    /// gets \p id; a built-in collection writes its override (a name equal to the
    /// built-in name in English or in the language of the user interface is stored
    /// as not renamed, the built-in icon as NULL). Refused: an invalid name, a
    /// duplicate name (\p isDuplicate), no tool, an invalid tool, an unknown
    /// built-in collection, a custom collection which does not exist (deleted by
    /// another instance), a new collection over MAXIMUM_CUSTOM_COLLECTIONS (\p isLimit).
    PDFDatabaseError saveCollection(const PDFToolCollectionDraft& draft, qint64* id = nullptr, bool* isDuplicate = nullptr, bool* isLimit = nullptr);

    /// Creates a custom collection (saveCollection of a draft with id 0)
    PDFDatabaseError createCollection(const QString& name, const QString& iconName, const QStringList& toolIds,
                                      qint64* id = nullptr, bool* isDuplicate = nullptr, bool* isLimit = nullptr);

    /// Deletes a custom collection with its items; an override of a built-in
    /// collection is refused (resetBuiltin), a missing one is an error
    PDFDatabaseError deleteCollection(qint64 id);

    /// Deletes the custom collection, which the user has confirmed: the row of the
    /// id must still have the confirmed name (as getCollection reads it) and the
    /// confirmed time of the change - the ids are reused (no AUTOINCREMENT), so after
    /// a deletion by another instance the id may denote a new collection, which is
    /// never deleted (review RS6); a changed row is refused, a missing one is an error
    PDFDatabaseError deleteCollection(qint64 id, const QString& expectedName, qint64 expectedModified);

    /// Reset to default: deletes the override of the built-in collection (its items
    /// with it); nothing to delete is a success
    PDFDatabaseError resetBuiltin(const QString& builtinId);

    /// Adds the tool at the end of the collection, or removes it (\p isIncluded false;
    /// Add to collection of the context menu). The collection is a custom one
    /// (\p collectionId) or a built-in one (\p builtinId, \p collectionId ignored): a
    /// built-in collection without an override gets one with its default tools and
    /// the change. A tool, which is already there (or not there), is a success
    /// without a write; the last tool of a collection is not removed (refused). A
    /// change, which gives the built-in definition again (not renamed, the default
    /// icon, its tools in their order), deletes the override (as saveCollection).
    PDFDatabaseError setToolIncluded(qint64 collectionId, const QString& builtinId, const QString& toolId, bool isIncluded);

    /// Adds the tool (setToolIncluded with true)
    PDFDatabaseError addTool(qint64 collectionId, const QString& builtinId, const QString& toolId);

private:
    qint64 getCurrentTime() const;
    PDFDatabaseConnection* getConnection() const;
    /// Rows of the condition; without \p isItemsRead the tools are not read (the
    /// check of the names inside of the write transaction)
    std::vector<PDFToolCollectionRow> readRows(const char* condition, const QVariant& parameter, bool isItemsRead = true) const;
    QStringList readItems(PDFDatabaseConnection* connection, qint64 id) const;
    static PDFDatabaseError writeItems(PDFDatabaseConnection* connection, qint64 id, const QStringList& toolIds);
    static PDFDatabaseError validateTools(const QStringList& toolIds);
    PDFDatabaseError deleteCustomCollection(qint64 id, const QString* expectedName, qint64 expectedModified);

    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFTOOLCOLLECTIONREPOSITORY_H
