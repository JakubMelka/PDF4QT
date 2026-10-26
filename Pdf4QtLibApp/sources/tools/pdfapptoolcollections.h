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

#ifndef PDFAPPTOOLCOLLECTIONS_H
#define PDFAPPTOOLCOLLECTIONS_H

#include "pdfappglobal.h"
#include "pdftoolcollectionrepository.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

/// Collection of tools as the pages show it (plan of step 6, 4.1): a built-in
/// collection merged with its override, or a custom collection
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCollection
{
public:
    /// Identifier of the routes /tools/collections/{collectionId} and
    /// /tools?collection=: the id of a built-in collection ("prepare-for-sharing")
    /// or "custom-<row id>"
    QString routeId;

    /// Built-in collection (empty for a custom one)
    QString builtinId;

    /// Row of the database: the custom collection or the override (0 = a built-in
    /// collection, which the user has not changed)
    qint64 rowId = 0;

    /// Shown name (the translated built-in name unless the user has renamed it)
    QString name;

    /// Icon of the design (always one of PDFAppBuiltinToolCollections::getIconChoices)
    QString iconName;

    /// Tools in their order (valid identifiers of the catalog)
    QStringList toolIds;

    /// Milliseconds since the epoch of the last change (0 = never changed / unknown)
    qint64 modified = 0;

    bool isBuiltin = false;
    bool isOverridden = false;      ///< A built-in collection changed by the user (Reset to default is offered)
};

/// Merge of the built-in collections and of the stored rows into the collections
/// of the pages: the built-in collections in their order (with their overrides),
/// then the custom collections by their id. Data only, thread safe.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCollections
{
public:
    PDFAppToolCollections() = delete;

    /// Prefix of the route id of a custom collection
    static QString getCustomPrefix() { return QStringLiteral("custom-"); }

    /// Collections of the stored rows (PDFToolCollectionRepository::getCollections)
    static std::vector<PDFAppToolCollection> merge(const std::vector<PDFToolCollectionRow>& rows);

    /// Collections read from the database (one read transaction)
    static std::vector<PDFAppToolCollection> read(PDFDatabase* database);

    /// Collection of the route id, nullptr = none
    static const PDFAppToolCollection* find(const std::vector<PDFAppToolCollection>& collections, const QString& routeId);

    /// Route id of a custom collection ("custom-12")
    static QString getCustomRouteId(qint64 rowId);

    /// Row id of the route id of a custom collection, 0 = not a route id of a custom
    /// collection. The parser is bounded: "custom-" and 1 to 18 digits without a
    /// leading zero ("custom-0", "custom-012", an overflow, a sign are refused).
    static qint64 parseCustomRouteId(const QString& routeId);

    /// The route id has the syntax of a collection (a built-in id or a custom route id);
    /// it says nothing about the existence of a custom collection
    static bool isValidRouteId(const QString& routeId);

    /// Most digits of the row id of a custom route id
    static constexpr int MAXIMUM_ROW_ID_DIGITS = 18;
};

}   // namespace pdf

#endif // PDFAPPTOOLCOLLECTIONS_H
