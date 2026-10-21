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

#ifndef PDFTOOLREPOSITORY_H
#define PDFTOOLREPOSITORY_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QPointer>
#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

/// Use of a tool (table tool_usage): written when the user accepts the result of
/// the tool (phase 2, newapp_tools.md 3)
class PDF4QTLIBAPPSHARED_EXPORT PDFToolUsage
{
public:
    QString toolId;         ///< Identifier of PDFDocumentToolCatalog
    qint64 lastUsed = 0;    ///< Milliseconds since the epoch
    qint64 count = 0;
};

/// Pinned tools and the use of the tools (tables tool_pin, tool_usage) - what
/// the pages of step 2 need: the star of the tool rows (What's New New tools,
/// the catalog in step 6), Recent Tools of Home, the filter For you of Support
/// and the search. The collections, pairs and parameters stay for step 6 and
/// phase 2. Only identifiers of PDFDocumentToolCatalog are written; rows with an
/// unknown identifier (another version) are skipped by the reads. A value
/// object: reads go to the database, writes are short BEGIN IMMEDIATE
/// transactions, notified with the topics getPinTopic / getUsageTopic (key =
/// tool id). Usable in any thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFToolRepository
{
public:
    static QString getPinTopic() { return QStringLiteral("tool_pin"); }
    static QString getUsageTopic() { return QStringLiteral("tool_usage"); }

    explicit PDFToolRepository(PDFDatabase* database);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// Pinned tools in their order
    QStringList getPinnedTools() const;

    bool isPinned(const QString& toolId) const;

    /// Pins (at the end) / unpins the tool; an unknown tool id is refused
    PDFDatabaseError setPinned(const QString& toolId, bool pinned);

    /// Order of the pinned tools: the listed ids first, the other pinned tools after them
    PDFDatabaseError setPinnedOrder(const QStringList& toolIds);

    /// Use of the tools, the most recently used first (Recent Tools of Home: the first rows)
    std::vector<PDFToolUsage> getUsage(int limit = -1) const;

    /// Identifiers of the tools, which were ever used (filter For you of Support)
    QStringList getUsedTools() const;

    /// Records an accepted result of the tool: count + 1, last_used = now (phase 2;
    /// the runs of MCP are not recorded, newapp_tools.md 3)
    PDFDatabaseError recordUsage(const QString& toolId);

private:
    qint64 getCurrentTime() const;
    PDFDatabaseConnection* getConnection() const;

    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFTOOLREPOSITORY_H
