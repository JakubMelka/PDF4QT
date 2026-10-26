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

#ifndef PDFDOCUMENTTOOLUSAGE_H
#define PDFDOCUMENTTOOLUSAGE_H

#include "pdfappglobal.h"
#include "pdfdatabase.h"

#include <QPointer>
#include <QString>
#include <QStringList>

namespace pdf
{

class PDFDocumentToolAvailability;

/// Use and chaining of the tools (newapp_tools.md 3, owner decision O21): the
/// writes of an accepted run and the suggestions of Then continue with. The
/// rules themselves are the pure functions of PDFDocumentToolSuggestions (Core);
/// this class reads and writes the tables tool_usage and tool_pair and the state
/// setting tools.historyRuns. A value object usable in any thread (each thread
/// has its own connection); every write is one BEGIN IMMEDIATE transaction, so
/// two instances writing at once lose nothing. Only tool ids and numbers are
/// stored, never file names. PROVISIONAL until the pilot tool Compress of phase 2
/// (the result dialog calls recordRun; MCP passes Origin::Mcp).
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolUsage
{
public:
    /// Who ran the tool
    enum class Origin
    {
        User,   ///< The user in the GUI - counted
        Mcp     ///< An assistant through MCP - never counted (newapp_tools.md 3)
    };

    /// Most rows of tool_pair of one tool read for an update
    static constexpr int MAXIMUM_PAIR_ROWS = 1000;

    explicit PDFDocumentToolUsage(PDFDatabase* database);

    /// Fixed time of the writes (tests); 0 = the current time
    void setCurrentTime(qint64 time) { m_currentTime = time; }

    /// Records a run of \p toolId, whose result was accepted (or which ended with
    /// NothingToDo), in one transaction: tool_usage (count + 1, last_used = now -
    /// Recent tools); when \p previousToolId is the tool, after whose result this
    /// one ran (Then continue with, or the first next tool over the result in the
    /// same tab), and the setting tools.suggestNext is on, also the pair previous →
    /// tool (PDFDocumentToolSuggestions::recordPair: the default successors at the
    /// first write, the aging) and tools.historyRuns + 1. With the setting off only
    /// the use is recorded (Recent tools is not the tool history). Origin::Mcp
    /// records nothing. An unknown \p toolId is refused; an unknown or the same
    /// \p previousToolId records no pair.
    PDFDatabaseError recordRun(const QString& toolId, const QString& previousToolId = QString(), Origin origin = Origin::User);

    /// Suggestions of Then continue with after \p finishedToolId (at most three,
    /// read in one read transaction: the setting, the pairs of the tool and the
    /// default successors of the catalog); the tools, which cannot run in
    /// \p availability, are skipped
    QStringList getSuggestions(const QString& finishedToolId, const PDFDocumentToolAvailability& availability) const;

    /// The setting tools.suggestNext (a hostile stored value = the default, on)
    bool isSuggestNextEnabled() const;

private:
    qint64 getCurrentTime() const;
    PDFDatabaseConnection* getConnection() const;

    QPointer<PDFDatabase> m_database;
    qint64 m_currentTime = 0;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLUSAGE_H
