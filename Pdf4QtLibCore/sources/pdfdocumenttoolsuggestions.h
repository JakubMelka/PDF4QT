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

#ifndef PDFDOCUMENTTOOLSUGGESTIONS_H
#define PDFDOCUMENTTOOLSUGGESTIONS_H

#include "pdfglobal.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{

/// Counter of a pair of tools run one after another (a row of the table
/// tool_pair: the tool \p toTool ran after \p fromTool, newapp_tools.md 3)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolPair
{
public:
    QString fromTool;       ///< Identifier of the tool, which ran first
    QString toTool;         ///< Identifier of the tool, which ran next
    qint64 count = 0;       ///< Counter (0 and less = no pair)
    qint64 lastUsed = 0;    ///< Milliseconds since the epoch of the last run of the pair

    bool operator==(const PDFDocumentToolPair&) const = default;
};

/// Input of the suggestions of the next tools
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSuggestionInput
{
public:
    /// Tools, after which the suggested tools run: the finished tool of Then
    /// continue with (one), the tools of a collection ("Suggested for this
    /// collection" of the collections editor, more)
    QStringList fromTools;

    /// Counters of the pairs of the tools of fromTools (the rows of tool_pair,
    /// pairs of other tools are ignored); untrusted - invalid identifiers and
    /// counters are left out
    std::vector<PDFDocumentToolPair> pairs;

    /// Default successors of each tool of fromTools; std::nullopt = the default
    /// successors of the catalog (PDFDocumentToolCatalogEntry::getDefaultSuccessors)
    std::optional<QStringList> defaultSuccessors;

    /// Tools, which are never suggested (the tools of the collection; the tools of
    /// fromTools are never suggested anyway)
    QStringList excludedTools;

    /// Tools, which cannot run now (a locked document, a missing Tesseract or
    /// scanner, an unsuitable kind of the result) - skipped (O21)
    QStringList unavailableTools;

    /// Setting tools.suggestNext: false = only the default successors, the counters
    /// of the user are not used (O21)
    bool isSuggestNextEnabled = true;

    /// Fill the suggestions up by the tools of the categories of fromTools in the
    /// order of the catalog (the collections editor: "the same category")
    bool isSameCategoryFilled = false;

    /// Most suggested tools (bounded to PDFDocumentToolSuggestions::MAXIMUM_SUGGESTIONS)
    int maximumCount = 3;
};

/// What can run after a finished tool: the tools, which cannot run, are not
/// suggested (O21: a locked document, a missing Tesseract or scanner, an
/// unsuitable kind of the result)
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolAvailability
{
public:
    /// A document is open after the finished tool (its result or the analyzed
    /// document); false - e.g. after an export of files without a document
    bool hasDocument = true;

    /// The document is locked (tools.md 4.7): the tools, which change it, cannot run
    bool isDocumentLocked = false;

    /// Tesseract and its language models are available
    bool isTesseractAvailable = true;

    /// A scanner backend is available
    bool isScannerAvailable = true;
};

/// Suggestions of the next tools (newapp_tools.md 3, owner decision O21): a pure
/// function of the counters of the pairs, the default successors of the catalog,
/// the availability of the tools and the setting. It is used by Then continue
/// with of the result dialog (phase 2), "Often used next" of the introduction page
/// and "Suggested for this collection" of the collections editor (step 6). The
/// counters are read and written by PDFDocumentToolUsage of Pdf4QtLibApp.
///
/// Rules: at most three tools with the highest counters; a tie is won by a default
/// successor (O25), then decided by the newer last use, then by the order of the
/// catalog. A pair of the user is
/// suggested from the counter MINIMUM_USER_COUNT (a single attempt is not). The
/// default successors are inserted with DEFAULT_SUCCESSOR_COUNT at the first write
/// of a pair of the tool; until then only they are suggested. A tool, which
/// cannot run, is skipped. When the sum of the counters of a tool exceeds
/// AGING_LIMIT, all its counters are halved (rounded down) and zeros are removed.
/// With the setting tools.suggestNext off only the default successors are
/// suggested (the counters are neither used nor written).
///
/// Consequence of the rules: a pair of the user ties with the inserted default
/// successors at its second use (both 2) and loses the tie, so a tool with three
/// default successors keeps them; at the third use the pair of the user is ahead
/// of them and replaces the last default (in the order of the catalog).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSuggestions
{
public:
    PDFDocumentToolSuggestions() = delete;

    /// Most suggested tools
    static constexpr int MAXIMUM_SUGGESTIONS = 3;

    /// Smallest counter of a pair of the user, which is suggested
    static constexpr qint64 MINIMUM_USER_COUNT = 2;

    /// Counter of a default successor inserted at the first write of a pair
    static constexpr qint64 DEFAULT_SUCCESSOR_COUNT = 2;

    /// Sum of the counters of a tool, over which its counters are halved
    static constexpr qint64 AGING_LIMIT = 50;

    /// Suggested tools (identifiers of the catalog), at most input.maximumCount
    /// (bounded to MAXIMUM_SUGGESTIONS), the best first; never a tool of
    /// fromTools, of excludedTools or of unavailableTools
    static QStringList suggest(const PDFDocumentToolSuggestionInput& input);

    /// Counters of the pairs of \p fromTool after the run of \p toTool after it
    /// (pure; PDFDocumentToolUsage writes the result in one transaction): the
    /// default successors are inserted at the first write (no pair of \p fromTool
    /// yet), the pair is increased, \p time is its last use, then the counters age
    /// (the pair just recorded keeps at least 1 - the run is never lost by the aging).
    /// Pairs of other tools and invalid rows of \p pairs are left out of the result.
    /// An invalid tool or a pair of a tool with itself gives \p pairs unchanged.
    static std::vector<PDFDocumentToolPair> recordPair(const std::vector<PDFDocumentToolPair>& pairs, const QString& fromTool, const QString& toTool, qint64 time);

    /// Aging of the counters of one tool: when their sum exceeds AGING_LIMIT, every
    /// counter is halved (rounded down) and the pairs with 0 are removed (pure)
    static std::vector<PDFDocumentToolPair> age(const std::vector<PDFDocumentToolPair>& pairs);

    /// The tool of the catalog can run in the situation (the locked access, the
    /// input kind and the external dependency of the catalog); an unknown id cannot
    static bool isAvailable(const QString& toolId, const PDFDocumentToolAvailability& availability);

    /// Tools of the catalog, which cannot run in the situation (the input
    /// unavailableTools of PDFDocumentToolSuggestionInput), in the order of the catalog
    static QStringList getUnavailableTools(const PDFDocumentToolAvailability& availability);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLSUGGESTIONS_H
