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

    // Words of every text of every field
    std::vector<std::vector<QStringList>> fieldWords(fields.size());
    for (size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
    {
        fieldWords[fieldIndex].reserve(size_t(fields[fieldIndex].texts.size()));
        for (const QString& text : fields[fieldIndex].texts)
        {
            fieldWords[fieldIndex].push_back(splitWords(text));
        }
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
            const std::vector<QStringList>& texts = fieldWords[fieldIndex];
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

        for (const QStringList& words : fieldWords[fieldIndex])
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

}   // namespace pdf
