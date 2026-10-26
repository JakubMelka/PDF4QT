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

#include "pdfapptoolstate.h"
#include "pdfdatabasestatement.h"
#include "pdfrepositoryhelper.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdftoolcollectionrepository.h"

#include "pdfdocumenttoolsuggestions.h"

namespace pdf
{

/// Helpers of the state of the tools
class PDFAppToolStateHelper
{
public:
    PDFAppToolStateHelper() = delete;

    /// The state setting tools.suggestNext: a stored 0 / 1, anything else is the
    /// default (on) - a row of another version is untrusted
    static bool readSuggestNext(PDFDatabase* database)
    {
        PDFDatabaseStatement statement(database->getConnection(), "SELECT CASE WHEN typeof(value) = 'integer' AND value IN (0, 1) THEN value END FROM setting WHERE key = ?1");
        statement.bindText(1, PDFSettingKeys::toolsSuggestNext());
        bool isEnabled = true;
        if (statement.next())
        {
            isEnabled = PDFRepositoryHelper::readInteger(statement, 0, 1, 0, 1) != 0;
            statement.reset();
        }
        return isEnabled;
    }

    /// Reads the state, \p isComplete also the collections and the setting
    static PDFAppToolState read(PDFDatabase* database, int usageLimit, bool isComplete)
    {
        PDFAppToolState state;
        if (!database)
        {
            if (isComplete)
            {
                state.collections = PDFAppToolCollections::merge(std::vector<PDFToolCollectionRow>());
            }
            return state;
        }

        // One consistent snapshot (another instance may write meanwhile)
        PDFDatabaseTransaction transaction(database->getConnection(), PDFDatabaseTransaction::Mode::Read);

        PDFToolRepository tools(database);
        state.pinnedTools = tools.getPinnedTools();
        state.usage = tools.getUsage(usageLimit);

        if (isComplete)
        {
            PDFToolCollectionRepository collections(database);
            state.collections = PDFAppToolCollections::merge(collections.getCollections());
            state.isSuggestNextEnabled = readSuggestNext(database);
        }

        if (transaction.isActive())
        {
            transaction.commit();
        }
        return state;
    }
};

PDFAppToolState PDFAppToolState::read(PDFDatabase* database, int usageLimit)
{
    return PDFAppToolStateHelper::read(database, usageLimit, true);
}

PDFAppToolState PDFAppToolState::readPinsAndUsage(PDFDatabase* database, int usageLimit)
{
    return PDFAppToolStateHelper::read(database, usageLimit, false);
}

std::vector<PDFDocumentToolPair> PDFAppToolState::readPairs(PDFDatabase* database, const QStringList& fromToolIds)
{
    if (!database)
    {
        return std::vector<PDFDocumentToolPair>();
    }
    return PDFToolRepository(database).getPairs(fromToolIds);
}

const PDFAppToolCollection* PDFAppToolState::findCollection(const QString& routeId) const
{
    return PDFAppToolCollections::find(collections, routeId);
}

qint64 PDFAppToolState::getLastUsed(const QString& toolId) const
{
    for (const PDFToolUsage& item : usage)
    {
        if (item.toolId == toolId)
        {
            return item.lastUsed;
        }
    }
    return 0;
}

bool PDFAppToolState::isRelevantTopic(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic())
    {
        return key.isEmpty() || key == PDFSettingKeys::toolsSuggestNext();
    }

    return topic == PDFToolRepository::getPinTopic() || topic == PDFToolRepository::getUsageTopic() || topic == PDFToolRepository::getPairTopic() ||
           topic == PDFToolCollectionRepository::getCollectionTopic() || topic == PDFToolCollectionRepository::getItemTopic();
}

}   // namespace pdf
