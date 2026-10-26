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

#include "pdfapptoolcollections.h"
#include "pdfappbuiltintoolcollections.h"
#include "pdfdatabasestatement.h"

namespace pdf
{

std::vector<PDFAppToolCollection> PDFAppToolCollections::merge(const std::vector<PDFToolCollectionRow>& rows)
{
    std::vector<PDFAppToolCollection> collections;

    for (const PDFAppBuiltinToolCollection& builtin : PDFAppBuiltinToolCollections::getCollections())
    {
        PDFAppToolCollection collection;
        collection.routeId = builtin.getId();
        collection.builtinId = builtin.getId();
        collection.name = builtin.getName();
        collection.iconName = builtin.getIconName();
        collection.toolIds = builtin.getToolIds();
        collection.isBuiltin = true;

        for (const PDFToolCollectionRow& row : rows)
        {
            if (row.builtinId == collection.builtinId)
            {
                collection.rowId = row.id;
                collection.isOverridden = true;
                collection.modified = row.modified;
                if (!row.name.isEmpty())
                {
                    collection.name = row.name;
                }
                if (PDFAppBuiltinToolCollections::isValidIconName(row.iconName))
                {
                    // Only an icon of the allowlist (as the custom collections below)
                    collection.iconName = row.iconName;
                }
                collection.toolIds = row.toolIds;
                break;
            }
        }

        collections.push_back(collection);
    }

    for (const PDFToolCollectionRow& row : rows)
    {
        if (row.isBuiltin() || row.id <= 0)
        {
            continue;
        }

        PDFAppToolCollection collection;
        collection.routeId = getCustomRouteId(row.id);
        collection.rowId = row.id;
        collection.name = row.name;
        collection.iconName = PDFAppBuiltinToolCollections::isValidIconName(row.iconName) ? row.iconName : PDFAppBuiltinToolCollections::getDefaultIconName();
        collection.toolIds = row.toolIds;
        collection.modified = row.modified;
        collections.push_back(collection);
    }

    return collections;
}

std::vector<PDFAppToolCollection> PDFAppToolCollections::read(PDFDatabase* database)
{
    // One snapshot of the collections and of their items
    PDFDatabaseTransaction transaction(database ? database->getConnection() : nullptr, PDFDatabaseTransaction::Mode::Read);
    PDFToolCollectionRepository repository(database);
    std::vector<PDFAppToolCollection> collections = merge(repository.getCollections());
    if (transaction.isActive())
    {
        transaction.commit();
    }
    return collections;
}

const PDFAppToolCollection* PDFAppToolCollections::find(const std::vector<PDFAppToolCollection>& collections, const QString& routeId)
{
    if (routeId.isEmpty())
    {
        return nullptr;
    }

    for (const PDFAppToolCollection& collection : collections)
    {
        if (collection.routeId == routeId)
        {
            return &collection;
        }
    }
    return nullptr;
}

QString PDFAppToolCollections::getCustomRouteId(qint64 rowId)
{
    return getCustomPrefix() + QString::number(rowId);
}

qint64 PDFAppToolCollections::parseCustomRouteId(const QString& routeId)
{
    const QString prefix = getCustomPrefix();
    if (!routeId.startsWith(prefix))
    {
        return 0;
    }

    const QStringView digits = QStringView(routeId).mid(prefix.size());
    if (digits.isEmpty() || digits.size() > MAXIMUM_ROW_ID_DIGITS || digits.front() == QLatin1Char('0'))
    {
        return 0;
    }

    qint64 value = 0;
    for (const QChar character : digits)
    {
        if (character.unicode() < u'0' || character.unicode() > u'9')
        {
            return 0;
        }
        value = value * 10 + (character.unicode() - u'0');
    }
    return value;
}

bool PDFAppToolCollections::isValidRouteId(const QString& routeId)
{
    return PDFAppBuiltinToolCollections::findCollection(routeId) || parseCustomRouteId(routeId) > 0;
}

}   // namespace pdf
