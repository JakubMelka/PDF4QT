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

#include "pdfdocumenttoolsearch.h"

#include <algorithm>
#include <array>

namespace pdf
{

/// Helpers of the normalization of the texts of the search
class PDFSearchQueryHelper
{
public:
    PDFSearchQueryHelper() = delete;

    /// The longest part of an untrusted text, which is looked at (the rest is
    /// cut before the work, a text of megabytes costs as much as a short one)
    static constexpr qsizetype MAXIMUM_INPUT_FACTOR = 8;

    static bool isInvisible(QChar::Category category)
    {
        return category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Separator_Line ||
               category == QChar::Separator_Paragraph || category == QChar::Other_NotAssigned;
    }

    static bool isMark(QChar::Category category)
    {
        return category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining || category == QChar::Mark_Enclosing;
    }

    static void appendCodePoint(QString& text, char32_t codePoint)
    {
        if (QChar::requiresSurrogates(codePoint))
        {
            text += QChar(QChar::highSurrogate(codePoint));
            text += QChar(QChar::lowSurrogate(codePoint));
        }
        else
        {
            text += QChar(char16_t(codePoint));
        }
    }

    /// Fast path of the normalization of a text of ASCII characters only
    static bool normalizeAscii(QStringView text, QString* result)
    {
        result->clear();
        result->reserve(text.size());
        for (const QChar character : text)
        {
            const char16_t value = character.unicode();
            if (value >= 0x80)
            {
                return false;
            }

            if ((value >= u'a' && value <= u'z') || (value >= u'0' && value <= u'9'))
            {
                *result += character;
            }
            else if (value >= u'A' && value <= u'Z')
            {
                *result += QChar(char16_t(value - u'A' + u'a'));
            }
            else
            {
                *result += QLatin1Char(' ');
            }
        }
        return true;
    }
};

int PDFSearchField::getScore(PDFSearchMatchQuality quality) const
{
    switch (quality)
    {
        case PDFSearchMatchQuality::Exact:
            return exactScore;
        case PDFSearchMatchQuality::Prefix:
            return prefixScore;
        case PDFSearchMatchQuality::Typo:
            return typoScore;
        case PDFSearchMatchQuality::None:
            break;
    }
    return 0;
}

PDFSearchField::PDFSearchField(QStringList texts, int exactScore, int prefixScore, int typoScore, bool isTitle) :
    texts(std::move(texts)),
    exactScore(exactScore),
    prefixScore(prefixScore),
    typoScore(typoScore),
    isTitle(isTitle)
{

}

void PDFSearchField::prepare()
{
    words.clear();
    words.reserve(size_t(texts.size()));
    for (const QString& text : texts)
    {
        words.push_back(PDFSearchQuery::splitWords(text));
    }
}

PDFSearchQuery::PDFSearchQuery(const QString& text) :
    m_text(sanitize(text))
{
    m_words = splitWords(m_text, MAXIMUM_WORDS);
    m_normalizedText = m_words.join(QLatin1Char(' '));
}

QString PDFSearchQuery::sanitize(const QString& text, qsizetype maximumLength)
{
    if (maximumLength <= 0)
    {
        return QString();
    }

    // Only the beginning of a huge text is looked at
    const QStringView input = QStringView(text).left(maximumLength * PDFSearchQueryHelper::MAXIMUM_INPUT_FACTOR);

    QString result;
    result.reserve(qMin(input.size(), maximumLength));
    bool isSpacePending = false;
    for (qsizetype i = 0; i < input.size(); ++i)
    {
        const QChar character = input[i];
        char32_t codePoint = character.unicode();
        qsizetype length = 1;
        if (character.isHighSurrogate() && i + 1 < input.size() && input[i + 1].isLowSurrogate())
        {
            codePoint = QChar::surrogateToUcs4(character, input[i + 1]);
            length = 2;
        }
        else if (character.isSurrogate())
        {
            // A lone surrogate is not a character
            continue;
        }

        if (QChar::isSpace(codePoint))
        {
            isSpacePending = !result.isEmpty();
        }
        else if (!PDFSearchQueryHelper::isInvisible(QChar::category(codePoint)))
        {
            const qsizetype needed = (isSpacePending ? 1 : 0) + length;
            if (result.size() + needed > maximumLength)
            {
                break;
            }

            if (isSpacePending)
            {
                result += QLatin1Char(' ');
                isSpacePending = false;
            }
            result += input.mid(i, length);
        }

        i += length - 1;
    }
    return result;
}

QString PDFSearchQuery::normalize(QStringView text)
{
    QString result;
    if (PDFSearchQueryHelper::normalizeAscii(text, &result))
    {
        return result;
    }

    const QString decomposed = text.toString().normalized(QString::NormalizationForm_KD);
    result.clear();
    result.reserve(decomposed.size());
    for (qsizetype i = 0; i < decomposed.size(); ++i)
    {
        const QChar character = decomposed[i];
        char32_t codePoint = character.unicode();
        if (character.isHighSurrogate() && i + 1 < decomposed.size() && decomposed[i + 1].isLowSurrogate())
        {
            codePoint = QChar::surrogateToUcs4(character, decomposed[i + 1]);
            ++i;
        }
        else if (character.isSurrogate())
        {
            result += QLatin1Char(' ');
            continue;
        }

        if (PDFSearchQueryHelper::isMark(QChar::category(codePoint)))
        {
            // The diacritics of the decomposed letters are left out (é -> e)
            continue;
        }

        if (QChar::isLetterOrNumber(codePoint))
        {
            PDFSearchQueryHelper::appendCodePoint(result, QChar::toCaseFolded(codePoint));
        }
        else
        {
            result += QLatin1Char(' ');
        }
    }
    return result;
}

QStringList PDFSearchQuery::splitWords(QStringView text, qsizetype maximumWords)
{
    QStringList words;
    if (maximumWords <= 0)
    {
        return words;
    }

    const QString normalized = normalize(text);
    for (QStringView word : QStringView(normalized).tokenize(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        QStringView cut = word.left(MAXIMUM_WORD_LENGTH);
        if (!cut.isEmpty() && cut.back().isHighSurrogate())
        {
            cut.chop(1);
        }
        if (cut.isEmpty())
        {
            continue;
        }

        words << cut.toString();
        if (words.size() >= maximumWords)
        {
            break;
        }
    }
    return words;
}

bool PDFSearchQuery::isWithinOneEdit(QStringView first, QStringView second)
{
    const qsizetype firstSize = first.size();
    const qsizetype secondSize = second.size();
    if (qAbs(firstSize - secondSize) > 1)
    {
        return false;
    }

    if (firstSize == secondSize)
    {
        qsizetype i = 0;
        while (i < firstSize && first[i] == second[i])
        {
            ++i;
        }

        if (i == firstSize)
        {
            return true;
        }

        // One changed character, or two adjacent characters swapped
        if (first.mid(i + 1) == second.mid(i + 1))
        {
            return true;
        }
        return i + 1 < firstSize && first[i] == second[i + 1] && first[i + 1] == second[i] && first.mid(i + 2) == second.mid(i + 2);
    }

    // One added or removed character
    const QStringView longer = firstSize > secondSize ? first : second;
    const QStringView shorter = firstSize > secondSize ? second : first;
    qsizetype i = 0;
    while (i < shorter.size() && shorter[i] == longer[i])
    {
        ++i;
    }
    return shorter.mid(i) == longer.mid(i + 1);
}

int PDFSearchQuery::getEditDistance(QStringView first, QStringView second, int limit)
{
    limit = std::max(limit, 0);
    const qsizetype firstSize = first.size();
    const qsizetype secondSize = second.size();
    if (firstSize > MAXIMUM_WORD_LENGTH || secondSize > MAXIMUM_WORD_LENGTH || qAbs(firstSize - secondSize) > limit)
    {
        return limit + 1;
    }

    // Optimal string alignment distance by three rows of the matrix (the row
    // before the previous one gives the swap of two adjacent characters)
    std::array<int, MAXIMUM_WORD_LENGTH + 1> beforePrevious = { };
    std::array<int, MAXIMUM_WORD_LENGTH + 1> previous = { };
    std::array<int, MAXIMUM_WORD_LENGTH + 1> current = { };
    for (qsizetype j = 0; j <= secondSize; ++j)
    {
        previous[size_t(j)] = int(j);
    }

    int previousRowMinimum = 0;
    for (qsizetype i = 1; i <= firstSize; ++i)
    {
        current[0] = int(i);
        int rowMinimum = current[0];
        for (qsizetype j = 1; j <= secondSize; ++j)
        {
            const int cost = first[i - 1] == second[j - 1] ? 0 : 1;
            int value = std::min({ previous[size_t(j)] + 1, current[size_t(j - 1)] + 1, previous[size_t(j - 1)] + cost });
            if (i > 1 && j > 1 && first[i - 1] == second[j - 2] && first[i - 2] == second[j - 1])
            {
                value = std::min(value, beforePrevious[size_t(j - 2)] + 1);
            }
            current[size_t(j)] = value;
            rowMinimum = std::min(rowMinimum, value);
        }

        // The minimum of a row is at least the smaller of the minimums of the two
        // rows before it (a swap reaches back two rows): two rows over the limit
        // keep every following row over it
        if (rowMinimum > limit && previousRowMinimum > limit)
        {
            return limit + 1;
        }

        previousRowMinimum = rowMinimum;
        beforePrevious = previous;
        previous = current;
    }

    return std::min(previous[size_t(secondSize)], limit + 1);
}

int PDFSearchQuery::getNearDistanceLimit(qsizetype length)
{
    if (length >= 8)
    {
        return 3;
    }
    if (length >= 5)
    {
        return 2;
    }
    return 0;
}

PDFSearchMatchQuality PDFSearchQuery::matchWord(QStringView queryWord, QStringView word)
{
    if (queryWord.isEmpty() || word.isEmpty())
    {
        return PDFSearchMatchQuality::None;
    }

    if (word.startsWith(queryWord))
    {
        return word.size() == queryWord.size() ? PDFSearchMatchQuality::Exact : PDFSearchMatchQuality::Prefix;
    }

    // A typo is never in the first character ("epub" is not "publish")
    if (queryWord.size() < MINIMUM_TYPO_LENGTH || queryWord.front() != word.front())
    {
        return PDFSearchMatchQuality::None;
    }

    if (isWithinOneEdit(queryWord, word))
    {
        return PDFSearchMatchQuality::Typo;
    }

    // A typo in the beginning of a longer word ("signt" of "signature"): the
    // beginnings of the word of the length of the query, one shorter and one
    // longer; a short query would find too much ("sign" is not "single")
    if (word.size() > queryWord.size() && queryWord.size() >= MINIMUM_PREFIX_TYPO_LENGTH)
    {
        for (const qsizetype length : { queryWord.size() - 1, queryWord.size(), queryWord.size() + 1 })
        {
            if (length > 0 && length < word.size() && isWithinOneEdit(queryWord, word.left(length)))
            {
                return PDFSearchMatchQuality::Typo;
            }
        }
    }

    return PDFSearchMatchQuality::None;
}

PDFSearchMatchQuality PDFSearchQuery::matchText(const QString& text) const
{
    if (isEmpty())
    {
        return PDFSearchMatchQuality::None;
    }

    const QStringList words = splitWords(text);
    PDFSearchMatchQuality weakest = PDFSearchMatchQuality::Exact;
    for (const QString& queryWord : m_words)
    {
        PDFSearchMatchQuality best = PDFSearchMatchQuality::None;
        for (const QString& word : words)
        {
            best = std::max(best, matchWord(queryWord, word));
            if (best == PDFSearchMatchQuality::Exact)
            {
                break;
            }
        }
        weakest = std::min(weakest, best);
        if (weakest == PDFSearchMatchQuality::None)
        {
            break;
        }
    }
    return weakest;
}

std::optional<PDFSearchFieldMatch> PDFSearchQuery::match(const std::vector<PDFSearchField>& fields) const
{
    if (isEmpty() || fields.empty())
    {
        return std::nullopt;
    }

    // Words of every text of every field (a prepared field has them already)
    std::vector<std::vector<QStringList>> splitTexts(fields.size());
    std::vector<const std::vector<QStringList>*> fieldWords(fields.size(), nullptr);
    for (size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
    {
        const PDFSearchField& field = fields[fieldIndex];
        if (field.isPrepared())
        {
            fieldWords[fieldIndex] = &field.words;
            continue;
        }

        splitTexts[fieldIndex].reserve(size_t(field.texts.size()));
        for (const QString& text : field.texts)
        {
            splitTexts[fieldIndex].push_back(splitWords(text));
        }
        fieldWords[fieldIndex] = &splitTexts[fieldIndex];
    }

    PDFSearchFieldMatch result;
    for (const QString& queryWord : m_words)
    {
        int bestScore = 0;
        int bestField = -1;
        int bestText = -1;
        for (size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
        {
            const PDFSearchField& field = fields[fieldIndex];
            const std::vector<QStringList>& texts = *fieldWords[fieldIndex];
            for (size_t textIndex = 0; textIndex < texts.size(); ++textIndex)
            {
                for (const QString& word : texts[textIndex])
                {
                    const int score = field.getScore(matchWord(queryWord, word));

                    // A more important field wins a tie (it comes first)
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestField = int(fieldIndex);
                        bestText = int(textIndex);
                    }
                }
            }
        }

        if (bestScore <= 0)
        {
            return std::nullopt;
        }

        result.score += bestScore;
        if (bestField > result.weakestField)
        {
            result.weakestField = bestField;
            result.weakestText = fields[size_t(bestField)].texts.value(bestText);
        }
    }

    // The whole title equal to the query, or starting by it
    int bonus = 0;
    for (size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
    {
        if (!fields[fieldIndex].isTitle)
        {
            continue;
        }

        for (const QStringList& words : *fieldWords[fieldIndex])
        {
            const QString title = words.join(QLatin1Char(' '));
            if (title == m_normalizedText)
            {
                bonus = std::max(bonus, int(EXACT_TITLE_BONUS));
            }
            else if (title.startsWith(m_normalizedText))
            {
                bonus = std::max(bonus, int(PREFIX_TITLE_BONUS));
            }
        }
    }
    result.score += bonus;
    return result;
}

std::optional<PDFDocumentToolSearchMatch> PDFDocumentToolSearch::matchTool(const PDFDocumentToolCatalogEntry& tool, const PDFSearchQuery& query)
{
    if (query.isEmpty())
    {
        return std::nullopt;
    }

    // The texts of the user interface and the English ones (once, when equal)
    QStringList titles = { tool.getTitle() };
    if (tool.getSourceTitle() != titles.front())
    {
        titles << tool.getSourceTitle();
    }

    QStringList keywords = tool.getTranslatedKeywords();
    for (const QString& keyword : tool.getKeywords())
    {
        if (!keywords.contains(keyword))
        {
            keywords << keyword;
        }
    }

    QStringList descriptions = { tool.getDescription() };
    if (tool.getSourceDescription() != descriptions.front())
    {
        descriptions << tool.getSourceDescription();
    }

    const std::vector<PDFSearchField> fields = { PDFSearchField(titles, TITLE_EXACT, TITLE_PREFIX, TITLE_TYPO, true),
                                                 PDFSearchField(keywords, KEYWORD_EXACT, KEYWORD_PREFIX, KEYWORD_TYPO),
                                                 PDFSearchField(descriptions, DESCRIPTION_EXACT, DESCRIPTION_PREFIX, 0) };

    const std::optional<PDFSearchFieldMatch> match = query.match(fields);
    if (!match)
    {
        return std::nullopt;
    }

    PDFDocumentToolSearchMatch result;
    result.tool = &tool;
    result.score = match->score;
    switch (match->weakestField)
    {
        case 1:
            result.field = PDFDocumentToolSearchMatch::Field::Keyword;
            result.keyword = match->weakestText;
            break;
        case 2:
            result.field = PDFDocumentToolSearchMatch::Field::Description;
            break;
        default:
            result.field = PDFDocumentToolSearchMatch::Field::Title;
            break;
    }
    return result;
}

std::vector<PDFDocumentToolSearchMatch> PDFDocumentToolSearch::search(const PDFSearchQuery& query)
{
    std::vector<PDFDocumentToolSearchMatch> result;
    if (query.isEmpty())
    {
        return result;
    }

    for (const PDFDocumentToolCatalogEntry& tool : PDFDocumentToolCatalog::getTools())
    {
        if (std::optional<PDFDocumentToolSearchMatch> match = matchTool(tool, query))
        {
            result.push_back(std::move(*match));
        }
    }

    // The order of the catalog decides a tie (a stable sort of the catalog order)
    std::stable_sort(result.begin(), result.end(), [](const PDFDocumentToolSearchMatch& left, const PDFDocumentToolSearchMatch& right) { return left.score > right.score; });
    return result;
}

std::vector<PDFDocumentToolSearchMatch> PDFDocumentToolSearch::search(const QString& text)
{
    return search(PDFSearchQuery(text));
}

/// Helpers of the nearest tools of a query
class PDFDocumentToolNearestHelper
{
public:
    PDFDocumentToolNearestHelper() = delete;

    /// Normalized words of the titles and of the keywords of a tool
    class ToolWords
    {
    public:
        const PDFDocumentToolCatalogEntry* tool = nullptr;
        int catalogIndex = 0;
        QStringList titleWords;
        std::vector<std::pair<QString, QStringList>> keywords;  ///< Keyword and its words
    };

    /// Best score of a word of the query against one word of the tool: the
    /// scores of search for a match of matchWord, a near score otherwise
    static int getWordScore(QStringView queryWord, QStringView word, int exactScore, int prefixScore, int typoScore, int nearScore)
    {
        switch (PDFSearchQuery::matchWord(queryWord, word))
        {
            case PDFSearchMatchQuality::Exact:
                return exactScore;
            case PDFSearchMatchQuality::Prefix:
                return prefixScore;
            case PDFSearchMatchQuality::Typo:
                return typoScore;
            case PDFSearchMatchQuality::None:
                break;
        }

        // Never a difference in the first character ("epub" is not "publish")
        const int limit = PDFSearchQuery::getNearDistanceLimit(queryWord.size());
        if (limit <= 0 || queryWord.isEmpty() || word.isEmpty() || queryWord.front() != word.front())
        {
            return 0;
        }

        // The whole word, or its beginning of about the length of the query
        int distance = PDFSearchQuery::getEditDistance(queryWord, word, limit);
        if (word.size() > queryWord.size())
        {
            for (const qsizetype length : { queryWord.size() - 1, queryWord.size(), queryWord.size() + 1 })
            {
                if (length > 0 && length < word.size())
                {
                    distance = std::min(distance, PDFSearchQuery::getEditDistance(queryWord, word.left(length), limit));
                }
            }
        }

        return distance <= limit ? std::max(nearScore - PDFDocumentToolSearch::NEAR_EDIT_PENALTY * distance, 1) : 0;
    }

    /// Best score of a word of the query against the words of a list
    static int getListScore(QStringView queryWord, const QStringList& words, int exactScore, int prefixScore, int typoScore, int nearScore)
    {
        int best = 0;
        for (const QString& word : words)
        {
            best = std::max(best, getWordScore(queryWord, word, exactScore, prefixScore, typoScore, nearScore));
            if (best == exactScore)
            {
                break;
            }
        }
        return best;
    }

    static ToolWords getToolWords(const PDFDocumentToolCatalogEntry& tool, int catalogIndex)
    {
        ToolWords result;
        result.tool = &tool;
        result.catalogIndex = catalogIndex;

        // The texts of the user interface and the English ones
        result.titleWords = PDFSearchQuery::splitWords(tool.getTitle());
        for (const QString& word : PDFSearchQuery::splitWords(tool.getSourceTitle()))
        {
            if (!result.titleWords.contains(word))
            {
                result.titleWords << word;
            }
        }

        QStringList keywords = tool.getTranslatedKeywords();
        for (const QString& keyword : tool.getKeywords())
        {
            if (!keywords.contains(keyword))
            {
                keywords << keyword;
            }
        }
        for (const QString& keyword : std::as_const(keywords))
        {
            result.keywords.emplace_back(keyword, PDFSearchQuery::splitWords(keyword));
        }
        return result;
    }

    /// Score of the tool for one word of the query; \p keyword gets the keyword,
    /// when a keyword gave the score (a title wins a tie)
    static int getToolScore(QStringView queryWord, const ToolWords& tool, QString* keyword)
    {
        using Search = PDFDocumentToolSearch;

        const int titleScore = getListScore(queryWord, tool.titleWords, Search::TITLE_EXACT, Search::TITLE_PREFIX, Search::TITLE_TYPO, Search::TITLE_NEAR);
        int keywordScore = 0;
        for (const auto& [text, words] : tool.keywords)
        {
            const int score = getListScore(queryWord, words, Search::KEYWORD_EXACT, Search::KEYWORD_PREFIX, Search::KEYWORD_TYPO, Search::KEYWORD_NEAR);
            if (score > keywordScore)
            {
                keywordScore = score;
                *keyword = text;
            }
        }

        if (titleScore >= keywordScore)
        {
            keyword->clear();
            return titleScore;
        }
        return keywordScore;
    }

    /// Candidate of the nearest tools
    class Candidate
    {
    public:
        const PDFDocumentToolCatalogEntry* tool = nullptr;
        int catalogIndex = 0;
        int matchedWords = 0;
        int score = 0;
        bool isDecisive = false;        ///< A decisive word of the query is near the tool
        int bestWordScore = 0;
        QString keyword;                ///< Keyword of the best word (empty = the title)
    };

    /// Order: more near words of the query, the higher score, the order of the catalog
    static bool isBefore(const Candidate& left, const Candidate& right)
    {
        if (left.matchedWords != right.matchedWords)
        {
            return left.matchedWords > right.matchedWords;
        }
        if (left.score != right.score)
        {
            return left.score > right.score;
        }
        return left.catalogIndex < right.catalogIndex;
    }
};

std::vector<PDFDocumentToolSearchMatch> PDFDocumentToolSearch::searchNearest(const PDFSearchQuery& query, const QStringList& excludedTools, int maximumCount)
{
    using Helper = PDFDocumentToolNearestHelper;

    std::vector<PDFDocumentToolSearchMatch> result;
    maximumCount = std::clamp(maximumCount, 0, MAXIMUM_NEAREST);
    if (query.isEmpty() || maximumCount == 0)
    {
        return result;
    }

    // Words of the query long enough to say something, each once
    QStringList queryWords;
    for (const QString& word : query.getWords())
    {
        if (word.size() >= MINIMUM_NEAR_WORD_LENGTH && !queryWords.contains(word))
        {
            queryWords << word;
        }
    }
    if (queryWords.isEmpty())
    {
        return result;
    }

    std::vector<Helper::ToolWords> tools;
    const std::vector<PDFDocumentToolCatalogEntry>& catalog = PDFDocumentToolCatalog::getTools();
    tools.reserve(catalog.size());
    for (size_t i = 0; i < catalog.size(); ++i)
    {
        tools.push_back(Helper::getToolWords(catalog[i], int(i)));
    }

    // Scores of each word of the query against each tool
    std::vector<Helper::Candidate> candidates(tools.size());
    for (size_t i = 0; i < tools.size(); ++i)
    {
        candidates[i].tool = tools[i].tool;
        candidates[i].catalogIndex = tools[i].catalogIndex;
    }

    for (const QString& queryWord : std::as_const(queryWords))
    {
        std::vector<int> scores(tools.size(), 0);
        std::vector<QString> keywords(tools.size());
        int matchingTools = 0;
        for (size_t i = 0; i < tools.size(); ++i)
        {
            scores[i] = Helper::getToolScore(queryWord, tools[i], &keywords[i]);
            if (scores[i] > 0)
            {
                ++matchingTools;
            }
        }

        // A short word or a word near too many tools does not say, which tool the
        // user looks for
        const bool isDistinctive = queryWord.size() >= MINIMUM_DECISIVE_WORD_LENGTH && matchingTools <= MAXIMUM_COMMON_WORD_TOOLS;
        for (size_t i = 0; i < tools.size(); ++i)
        {
            if (scores[i] <= 0)
            {
                continue;
            }

            Helper::Candidate& candidate = candidates[i];
            ++candidate.matchedWords;
            candidate.score += scores[i];
            candidate.isDecisive = candidate.isDecisive || (isDistinctive && scores[i] >= MINIMUM_DECISIVE_SCORE);
            if (scores[i] > candidate.bestWordScore)
            {
                candidate.bestWordScore = scores[i];
                candidate.keyword = keywords[i];
            }
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), Helper::isBefore);
    for (const Helper::Candidate& candidate : candidates)
    {
        if (int(result.size()) >= maximumCount)
        {
            break;
        }
        if (!candidate.isDecisive || candidate.matchedWords == 0 || excludedTools.contains(candidate.tool->getId()))
        {
            continue;
        }

        PDFDocumentToolSearchMatch match;
        match.tool = candidate.tool;
        match.score = candidate.score;
        match.field = candidate.keyword.isEmpty() ? PDFDocumentToolSearchMatch::Field::Title : PDFDocumentToolSearchMatch::Field::Keyword;
        match.keyword = candidate.keyword;
        result.push_back(std::move(match));
    }
    return result;
}

std::vector<PDFDocumentToolSearchMatch> PDFDocumentToolSearch::searchNearest(const QString& text, const QStringList& excludedTools, int maximumCount)
{
    return searchNearest(PDFSearchQuery(text), excludedTools, maximumCount);
}

}   // namespace pdf
