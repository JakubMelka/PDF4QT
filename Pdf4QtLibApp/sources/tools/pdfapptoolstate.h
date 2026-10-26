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

#ifndef PDFAPPTOOLSTATE_H
#define PDFAPPTOOLSTATE_H

#include "pdfappglobal.h"
#include "pdfapptoolcollections.h"
#include "pdftoolrepository.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

class PDFDatabase;

/// State of the tools of the user (plan of step 6, 4.1): one consistent snapshot
/// of the pins, the use and the collections read in one short read transaction.
/// A value: the pages read it, when they are shown and after a relevant
/// notification (isRelevantTopic), and keep no cache (newapp_storage.md 1).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolState
{
public:
    /// Pinned tools in their order (valid identifiers of the catalog)
    QStringList pinnedTools;

    /// Use of the tools, the most recently used first
    std::vector<PDFToolUsage> usage;

    /// Built-in collections (with their overrides) and the custom collections
    std::vector<PDFAppToolCollection> collections;

    /// Value of the setting tools.suggestNext (the suggestions of the next tools)
    bool isSuggestNextEnabled = true;

    /// Reads the state. \p usageLimit limits the rows of the use (-1 = all, the
    /// Tools page shows 5, the Tools panel 3). Without a database the state is empty
    /// (the built-in collections only).
    static PDFAppToolState read(PDFDatabase* database, int usageLimit = -1);

    /// Reads only the pins and the use (the Tools panel of the document) in one read
    /// transaction; the collections stay empty and isSuggestNextEnabled the default.
    /// Without a database the state is empty.
    static PDFAppToolState readPinsAndUsage(PDFDatabase* database, int usageLimit);

    /// Counters of the pairs of the tools (on demand: the suggestions of the
    /// collections editor), whose first tool is one of \p fromToolIds (empty = all)
    static std::vector<PDFDocumentToolPair> readPairs(PDFDatabase* database, const QStringList& fromToolIds);

    /// The tool is pinned
    bool isPinned(const QString& toolId) const { return pinnedTools.contains(toolId); }

    /// Collection of the route id, nullptr = none
    const PDFAppToolCollection* findCollection(const QString& routeId) const;

    /// Last use of the tool (milliseconds since the epoch), 0 = never used
    qint64 getLastUsed(const QString& toolId) const;

    /// A notification of the topic (and key) changes the state: tool_pin,
    /// tool_usage, tool_collection, tool_collection_item, tool_pair and the setting
    /// tools.suggestNext. A page refreshes after such a notification, coalesced to
    /// one refresh per turn of the event loop.
    static bool isRelevantTopic(const QString& topic, const QString& key = QString());
};

}   // namespace pdf

#endif // PDFAPPTOOLSTATE_H
