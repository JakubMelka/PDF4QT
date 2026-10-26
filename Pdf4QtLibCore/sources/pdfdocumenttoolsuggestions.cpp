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

#include "pdfdocumenttoolsuggestions.h"
#include "pdfdocumenttoolcatalog.h"

#include <algorithm>
#include <limits>

namespace pdf
{

/// Helpers of the suggestions
class PDFDocumentToolSuggestionsHelper
{
public:
    PDFDocumentToolSuggestionsHelper() = delete;

    /// Candidate of the suggestions
    class Candidate
    {
    public:
        QString toolId;
        qint64 count = 0;
        qint64 lastUsed = 0;
        int catalogIndex = 0;
        bool isDefault = false;     ///< A default successor of a tool of fromTools
    };

    /// Order of the candidates: the higher counter, a default successor (O25: it wins
    /// a tie with a pair of the user), the newer last use, the order of the catalog
    static bool isBefore(const Candidate& left, const Candidate& right)
    {
        if (left.count != right.count)
        {
            return left.count > right.count;
        }
        if (left.isDefault != right.isDefault)
        {
            return left.isDefault;
        }
        if (left.lastUsed != right.lastUsed)
        {
            return left.lastUsed > right.lastUsed;
        }
        return left.catalogIndex < right.catalogIndex;
    }

    /// The pair is usable: valid tools, not a tool with itself, a positive counter
    static bool isValidPair(const PDFDocumentToolPair& pair)
    {
        return pair.count > 0 && pair.fromTool != pair.toTool &&
               PDFDocumentToolCatalog::isValidToolId(pair.fromTool) && PDFDocumentToolCatalog::isValidToolId(pair.toTool);
    }

    /// Saturated sum of two non-negative counters
    static qint64 add(qint64 left, qint64 right)
    {
        return left > std::numeric_limits<qint64>::max() - right ? std::numeric_limits<qint64>::max() : left + right;
    }

    /// Adds the counter to the candidate of the tool (the counters of more tools of
    /// a collection are summed, the newer last use is kept; a default successor of
    /// any of the tools makes the candidate a default)
    static void addCandidate(std::vector<Candidate>& candidates, const QString& toolId, qint64 count, qint64 lastUsed, bool isDefault)
    {
        for (Candidate& candidate : candidates)
        {
            if (candidate.toolId == toolId)
            {
                candidate.count = add(candidate.count, count);
                candidate.lastUsed = std::max(candidate.lastUsed, lastUsed);
                candidate.isDefault = candidate.isDefault || isDefault;
                return;
            }
        }

        Candidate candidate;
        candidate.toolId = toolId;
        candidate.count = count;
        candidate.lastUsed = lastUsed;
        candidate.isDefault = isDefault;
        candidate.catalogIndex = PDFDocumentToolCatalog::getToolIndex(toolId);
        candidates.push_back(candidate);
    }

    /// Default successors of the tool (the input or the catalog)
    static QStringList getDefaultSuccessors(const PDFDocumentToolSuggestionInput& input, const QString& fromTool)
    {
        if (input.defaultSuccessors)
        {
            return *input.defaultSuccessors;
        }
        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(fromTool);
        return entry ? entry->getDefaultSuccessors() : QStringList();
    }

    /// Valid default successors of the tool (the input or the catalog): not the tool
    /// itself, known to the catalog, each once, at most MAXIMUM_DEFAULT_SUCCESSORS
    static QStringList getValidDefaultSuccessors(const PDFDocumentToolSuggestionInput& input, const QString& fromTool)
    {
        QStringList successors;
        for (const QString& successor : getDefaultSuccessors(input, fromTool))
        {
            if (successor != fromTool && PDFDocumentToolCatalog::isValidToolId(successor) && !successors.contains(successor))
            {
                successors << successor;
                if (successors.size() >= PDFDocumentToolCatalog::MAXIMUM_DEFAULT_SUCCESSORS)
                {
                    break;
                }
            }
        }
        return successors;
    }

    static bool containsPair(const std::vector<PDFDocumentToolPair>& pairs, const QString& fromTool, const QString& toTool)
    {
        for (const PDFDocumentToolPair& pair : pairs)
        {
            if (pair.fromTool == fromTool && pair.toTool == toTool)
            {
                return true;
            }
        }
        return false;
    }

    /// Valid pairs of the tools of \p fromTools, each pair (from, to) once - the
    /// first row of an untrusted list wins
    static std::vector<PDFDocumentToolPair> getValidPairs(const std::vector<PDFDocumentToolPair>& pairs, const QStringList& fromTools)
    {
        std::vector<PDFDocumentToolPair> result;
        for (const PDFDocumentToolPair& pair : pairs)
        {
            if (!fromTools.contains(pair.fromTool) || !isValidPair(pair) || containsPair(result, pair.fromTool, pair.toTool))
            {
                continue;
            }
            result.push_back(pair);
        }
        return result;
    }

    /// The tool can be suggested: not a tool of fromTools, not excluded, not
    /// unavailable and not suggested yet
    static bool isSuggestable(const PDFDocumentToolSuggestionInput& input, const QStringList& fromTools, const QStringList& result, const QString& toolId)
    {
        return !fromTools.contains(toolId) && !input.excludedTools.contains(toolId) &&
               !input.unavailableTools.contains(toolId) && !result.contains(toolId);
    }
};

QStringList PDFDocumentToolSuggestions::suggest(const PDFDocumentToolSuggestionInput& input)
{
    using Helper = PDFDocumentToolSuggestionsHelper;

    QStringList result;
    const int maximumCount = std::clamp(input.maximumCount, 0, MAXIMUM_SUGGESTIONS);
    if (maximumCount == 0)
    {
        return result;
    }

    // Valid tools of fromTools, each once (untrusted, e.g. the items of a collection)
    QStringList fromTools;
    for (const QString& toolId : input.fromTools)
    {
        if (PDFDocumentToolCatalog::isValidToolId(toolId) && !fromTools.contains(toolId))
        {
            fromTools << toolId;
        }
    }

    // Counters of the user (only with the setting on) and the default successors
    // of each tool - the default successors only until the first pair of the tool
    // has been written (from then on they are rows of its pairs)
    const std::vector<PDFDocumentToolPair> pairs = input.isSuggestNextEnabled ? Helper::getValidPairs(input.pairs, fromTools) : std::vector<PDFDocumentToolPair>();
    std::vector<Helper::Candidate> candidates;
    for (const QString& fromTool : std::as_const(fromTools))
    {
        const QStringList successors = Helper::getValidDefaultSuccessors(input, fromTool);
        bool hasPairs = false;
        for (const PDFDocumentToolPair& pair : pairs)
        {
            if (pair.fromTool != fromTool)
            {
                continue;
            }

            hasPairs = true;
            if (pair.count >= MINIMUM_USER_COUNT)
            {
                Helper::addCandidate(candidates, pair.toTool, pair.count, pair.lastUsed, successors.contains(pair.toTool));
            }
        }

        if (!hasPairs)
        {
            for (const QString& successor : successors)
            {
                Helper::addCandidate(candidates, successor, DEFAULT_SUCCESSOR_COUNT, 0, true);
            }
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), Helper::isBefore);

    for (const Helper::Candidate& candidate : candidates)
    {
        if (result.size() >= maximumCount)
        {
            break;
        }
        if (Helper::isSuggestable(input, fromTools, result, candidate.toolId))
        {
            result << candidate.toolId;
        }
    }

    // The tools of the same categories in the order of the catalog
    if (input.isSameCategoryFilled && result.size() < maximumCount)
    {
        std::vector<PDFDocumentToolCategory> categories;
        for (const QString& fromTool : std::as_const(fromTools))
        {
            const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(fromTool);
            if (entry && std::find(categories.cbegin(), categories.cend(), entry->getCategory()) == categories.cend())
            {
                categories.push_back(entry->getCategory());
            }
        }

        for (const PDFDocumentToolCatalogEntry& tool : PDFDocumentToolCatalog::getTools())
        {
            if (result.size() >= maximumCount)
            {
                break;
            }

            const QString toolId = tool.getId();
            const bool isSameCategory = std::find(categories.cbegin(), categories.cend(), tool.getCategory()) != categories.cend();
            if (isSameCategory && Helper::isSuggestable(input, fromTools, result, toolId))
            {
                result << toolId;
            }
        }
    }

    return result;
}

std::vector<PDFDocumentToolPair> PDFDocumentToolSuggestions::recordPair(const std::vector<PDFDocumentToolPair>& pairs, const QString& fromTool, const QString& toTool, qint64 time)
{
    using Helper = PDFDocumentToolSuggestionsHelper;

    if (fromTool == toTool || !PDFDocumentToolCatalog::isValidToolId(fromTool) || !PDFDocumentToolCatalog::isValidToolId(toTool))
    {
        return pairs;
    }

    std::vector<PDFDocumentToolPair> result = Helper::getValidPairs(pairs, QStringList({ fromTool }));

    // The first write of a pair of the tool inserts its default successors
    if (result.empty())
    {
        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(fromTool);
        for (const QString& successor : entry ? entry->getDefaultSuccessors() : QStringList())
        {
            PDFDocumentToolPair pair;
            pair.fromTool = fromTool;
            pair.toTool = successor;
            pair.count = DEFAULT_SUCCESSOR_COUNT;
            pair.lastUsed = 0;
            result.push_back(pair);
        }
    }

    bool isFound = false;
    for (PDFDocumentToolPair& pair : result)
    {
        if (pair.toTool == toTool)
        {
            pair.count = Helper::add(pair.count, 1);
            pair.lastUsed = std::max<qint64>(time, 0);
            isFound = true;
        }
    }

    if (!isFound)
    {
        PDFDocumentToolPair pair;
        pair.fromTool = fromTool;
        pair.toTool = toTool;
        pair.count = 1;
        pair.lastUsed = std::max<qint64>(time, 0);
        result.push_back(pair);
    }

    // The run, which caused the write, is never lost by the aging: the pair just
    // recorded keeps at least 1 (a new successor of a tool at the limit gets its counter)
    result = age(result);
    if (!Helper::containsPair(result, fromTool, toTool))
    {
        PDFDocumentToolPair pair;
        pair.fromTool = fromTool;
        pair.toTool = toTool;
        pair.count = 1;
        pair.lastUsed = std::max<qint64>(time, 0);
        result.push_back(pair);
    }
    return result;
}

std::vector<PDFDocumentToolPair> PDFDocumentToolSuggestions::age(const std::vector<PDFDocumentToolPair>& pairs)
{
    using Helper = PDFDocumentToolSuggestionsHelper;

    qint64 sum = 0;
    for (const PDFDocumentToolPair& pair : pairs)
    {
        sum = Helper::add(sum, std::max<qint64>(pair.count, 0));
    }

    if (sum <= AGING_LIMIT)
    {
        return pairs;
    }

    std::vector<PDFDocumentToolPair> result;
    for (PDFDocumentToolPair pair : pairs)
    {
        pair.count = std::max<qint64>(pair.count, 0) / 2;
        if (pair.count > 0)
        {
            result.push_back(pair);
        }
    }
    return result;
}

bool PDFDocumentToolSuggestions::isAvailable(const QString& toolId, const PDFDocumentToolAvailability& availability)
{
    const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(toolId);
    if (!entry)
    {
        return false;
    }

    switch (entry->getExternalDependency())
    {
        case PDFDocumentToolExternalDependency::None:
            break;

        case PDFDocumentToolExternalDependency::Tesseract:
            if (!availability.isTesseractAvailable)
            {
                return false;
            }
            break;

        case PDFDocumentToolExternalDependency::Scanner:
            if (!availability.isScannerAvailable)
            {
                return false;
            }
            break;
    }

    if (entry->getInputKind() != PDFDocumentToolInputKind::None && !availability.hasDocument)
    {
        return false;
    }

    return !availability.isDocumentLocked || entry->getLockedAccess() != PDFDocumentToolLockedAccess::Unavailable;
}

QStringList PDFDocumentToolSuggestions::getUnavailableTools(const PDFDocumentToolAvailability& availability)
{
    QStringList tools;
    for (const PDFDocumentToolCatalogEntry& tool : PDFDocumentToolCatalog::getTools())
    {
        const QString toolId = tool.getId();
        if (!isAvailable(toolId, availability))
        {
            tools << toolId;
        }
    }
    return tools;
}

}   // namespace pdf
