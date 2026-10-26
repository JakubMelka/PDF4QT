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

#ifndef PDFDOCUMENTTOOLSEARCH_H
#define PDFDOCUMENTTOOLSEARCH_H

#include "pdfglobal.h"
#include "pdfdocumenttoolcatalog.h"

#include <QString>
#include <QStringList>
#include <QStringView>

#include <limits>
#include <optional>
#include <vector>

namespace pdf
{

/// Quality of the match of a word of a query with a word of a text (the order is the rank)
enum class PDFSearchMatchQuality
{
    None,       ///< The word does not match
    Typo,       ///< The word matches with one typo (one changed, added, removed or two swapped characters)
    Prefix,     ///< The word is the beginning of the word of the text
    Exact       ///< The words are equal
};

/// Field of a searched item (title, keywords, description, ...): its texts and
/// the score of a word of the query matched in one of them. A field with the
/// score 0 of a quality does not match by that quality (the typos of long texts).
class PDF4QTLIBCORESHARED_EXPORT PDFSearchField
{
public:
    PDFSearchField() = default;
    PDFSearchField(QStringList texts, int exactScore, int prefixScore, int typoScore, bool isTitle = false);

    /// Texts of the field (one title, the keywords, ...); the texts of the user
    /// interface and their English source texts are given both
    QStringList texts;

    int exactScore = 0;
    int prefixScore = 0;
    int typoScore = 0;

    /// The field is the title of the item: a title equal to the whole query, or
    /// starting by it, gets a bonus (exact title > title prefix > a word)
    bool isTitle = false;

    /// Normalized words of each text (PDFSearchQuery::splitWords), filled by
    /// \p prepare: a field searched by many queries (the files of Recent, while
    /// the user types) is normalized once. An unprepared field is split by each
    /// match; prepare the field again after a change of its texts.
    std::vector<QStringList> words;

    /// Splits the texts into their normalized words
    void prepare();

    /// The words of the texts are prepared
    bool isPrepared() const { return words.size() == size_t(texts.size()); }

    /// Score of the quality
    int getScore(PDFSearchMatchQuality quality) const;
};

/// Match of a query with the fields of an item
class PDF4QTLIBCORESHARED_EXPORT PDFSearchFieldMatch
{
public:
    /// Score (higher is better), 0 never happens for a match
    int score = 0;

    /// The least important field, in which a word of the query was found (index
    /// into the fields: the fields are given from the most important one)
    int weakestField = -1;

    /// Text of the weakest field, which matched (the reason of the match: the
    /// keyword "combine" of Merge)
    QString weakestText;
};

/// Query of a search (newapp_tools.md 2, the global search): the text of the user
/// is untrusted - it is cut to MAXIMUM_LENGTH characters, invisible characters are
/// removed, it is normalized (compatibility decomposition, without the combining
/// marks, case folded) and split into at most MAXIMUM_WORDS words of letters and
/// digits. Every word of the query must match a word of the item: equal, as its
/// beginning or (words of MINIMUM_TYPO_LENGTH characters and more) with one typo,
/// which is never in the first character.
class PDF4QTLIBCORESHARED_EXPORT PDFSearchQuery
{
public:
    /// Longest text of a query, which is used (a longer text is cut)
    static constexpr qsizetype MAXIMUM_LENGTH = 256;

    /// Most words of a query (the other words are left out)
    static constexpr qsizetype MAXIMUM_WORDS = 16;

    /// Longest word, which is compared (longer words are cut)
    static constexpr qsizetype MAXIMUM_WORD_LENGTH = 64;

    /// Shortest word of a query, which may have a typo
    static constexpr qsizetype MINIMUM_TYPO_LENGTH = 4;

    /// Shortest word of a query, which may have a typo as the beginning of a longer word
    static constexpr qsizetype MINIMUM_PREFIX_TYPO_LENGTH = 5;

    /// Bonus of a title equal to the whole query and of a title starting by it
    static constexpr int EXACT_TITLE_BONUS = 400;
    static constexpr int PREFIX_TITLE_BONUS = 200;

    PDFSearchQuery() = default;
    explicit PDFSearchQuery(const QString& text);

    /// The query has no word (an empty text, spaces, punctuation)
    bool isEmpty() const { return m_words.isEmpty(); }

    /// Text of the query for the user: without the invisible characters, the
    /// white space simplified, cut to MAXIMUM_LENGTH
    const QString& getText() const { return m_text; }

    /// Normalized words joined by one space
    const QString& getNormalizedText() const { return m_normalizedText; }

    /// Normalized words
    const QStringList& getWords() const { return m_words; }

    /// Matches the query with the fields of an item (the most important field
    /// first). \returns std::nullopt, if a word of the query matches no field
    std::optional<PDFSearchFieldMatch> match(const std::vector<PDFSearchField>& fields) const;

    /// Matches the query with a text (a title); the quality of the weakest word
    PDFSearchMatchQuality matchText(const QString& text) const;

    /// Text without the invisible characters (controls, format characters, line
    /// and paragraph separators, lone surrogates), the white space simplified,
    /// cut to \p maximumLength characters (never inside of a surrogate pair)
    static QString sanitize(const QString& text, qsizetype maximumLength = MAXIMUM_LENGTH);

    /// Normalized text: compatibility decomposition, the combining marks left
    /// out, case folded; every character other than a letter or a digit is a space
    static QString normalize(QStringView text);

    /// Normalized words of the text (at most \p maximumWords, each cut to MAXIMUM_WORD_LENGTH)
    static QStringList splitWords(QStringView text, qsizetype maximumWords = std::numeric_limits<qsizetype>::max());

    /// Match of a normalized word of a query with a normalized word of a text
    static PDFSearchMatchQuality matchWord(QStringView queryWord, QStringView word);

    /// The two words differ by at most one edit (a changed, added or removed
    /// character, or two adjacent characters swapped)
    static bool isWithinOneEdit(QStringView first, QStringView second);

    /// Number of edits (a changed, added or removed character, two adjacent
    /// characters swapped - the optimal string alignment distance) between the two
    /// words; \p limit + 1, when it is greater than \p limit (the computation stops
    /// early). Words longer than MAXIMUM_WORD_LENGTH give \p limit + 1.
    static int getEditDistance(QStringView first, QStringView second, int limit);

    /// Most edits of a word of a query of \p length characters, which is still near
    /// a word of a text (PDFDocumentToolSearch::searchNearest): 3 from 8 characters,
    /// 2 from 5 characters, 0 for shorter words (only the matches of matchWord)
    static int getNearDistanceLimit(qsizetype length);

private:
    QString m_text;
    QString m_normalizedText;
    QStringList m_words;
};

/// Tool of the catalog found by the search
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSearchMatch
{
public:
    /// Field of the tool, which decided the match
    enum class Field
    {
        Title,          ///< Every word is in the title
        Keyword,        ///< A word is only in a keyword
        Description     ///< A word is only in the description
    };

    /// The tool (a pointer into the static catalog)
    const PDFDocumentToolCatalogEntry* tool = nullptr;

    /// Score (higher is better)
    int score = 0;

    /// The least important field, which matched
    Field field = Field::Title;

    /// The keyword, which explains the match (Field::Keyword): "combine" for the
    /// query "comb" of Merge (shown as "matches “combine”", Figma 285:3026)
    QString keyword;
};

/// Search of the tools of the catalog (newapp_tools.md 2): one search for every
/// place (the catalog Tools, the group Tools of the global search, Another tool…
/// of Then continue with, adding a tool to a collection). Independent of the case
/// and of the diacritics, by the beginnings of the words, with the tolerance of
/// one typo; the order of a match is title -> keywords -> description, then the
/// order of the catalog. The translated texts and the English ones are searched.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolSearch
{
public:
    PDFDocumentToolSearch() = delete;

    /// Scores of the fields of a tool
    static constexpr int TITLE_EXACT = 100;
    static constexpr int TITLE_PREFIX = 90;
    static constexpr int TITLE_TYPO = 60;
    static constexpr int KEYWORD_EXACT = 50;
    static constexpr int KEYWORD_PREFIX = 45;
    static constexpr int KEYWORD_TYPO = 30;
    static constexpr int DESCRIPTION_EXACT = 20;
    static constexpr int DESCRIPTION_PREFIX = 18;

    /// Tools matching the text, the best first (an empty query finds nothing)
    static std::vector<PDFDocumentToolSearchMatch> search(const QString& text);
    static std::vector<PDFDocumentToolSearchMatch> search(const PDFSearchQuery& query);

    /// Match of one tool (std::nullopt, if it does not match)
    static std::optional<PDFDocumentToolSearchMatch> matchTool(const PDFDocumentToolCatalogEntry& tool, const PDFSearchQuery& query);

    /// Most tools offered by searchNearest
    static constexpr int MAXIMUM_NEAREST = 3;

    /// Shortest word of a query, which searchNearest uses ("to", "a" are left out)
    static constexpr qsizetype MINIMUM_NEAR_WORD_LENGTH = 3;

    /// Scores of a word of a query near a word of a title / a keyword
    /// (searchNearest), lowered by NEAR_EDIT_PENALTY for each edit
    static constexpr int TITLE_NEAR = 40;
    static constexpr int KEYWORD_NEAR = 20;
    static constexpr int NEAR_EDIT_PENALTY = 5;

    /// A word of a query offers a tool only, when it is decisive: it has at least
    /// MINIMUM_DECISIVE_WORD_LENGTH characters ("the", "and" of the keyword "front
    /// and back" are not), it is near at most MAXIMUM_COMMON_WORD_TOOLS tools ("pdf",
    /// "page" are too common) and its score is at least MINIMUM_DECISIVE_SCORE (a
    /// keyword three edits away is too weak). Other words add to the score only.
    static constexpr qsizetype MINIMUM_DECISIVE_WORD_LENGTH = 4;
    static constexpr int MAXIMUM_COMMON_WORD_TOOLS = 12;
    static constexpr int MINIMUM_DECISIVE_SCORE = 10;

    /// Nearest tools of a query, for which search finds nothing (owner decision
    /// O25, tools.md 9.1 point 4: the empty catalog search offers up to three
    /// nearest tools, nothing when no tool is close enough). Only the titles and the
    /// keywords (the texts of the user interface and the English ones) are compared.
    /// A word of the query (of at least MINIMUM_NEAR_WORD_LENGTH characters) is near
    /// a word of a tool, when it matches it as search does (equal, the beginning,
    /// one typo) or when it differs by at most getNearDistanceLimit edits from it or
    /// from its beginning - never in the first character. A tool is offered, when
    /// a decisive word of the query is near one of its words; a query of more words
    /// needs only some of them ("compress scan quality").
    /// Order: more near words of the query, the higher score, the order of the
    /// catalog. The tools of \p excludedTools (unavailable) are never offered; at
    /// most \p maximumCount (bounded to MAXIMUM_NEAREST) tools.
    static std::vector<PDFDocumentToolSearchMatch> searchNearest(const PDFSearchQuery& query, const QStringList& excludedTools = QStringList(), int maximumCount = MAXIMUM_NEAREST);
    static std::vector<PDFDocumentToolSearchMatch> searchNearest(const QString& text, const QStringList& excludedTools = QStringList(), int maximumCount = MAXIMUM_NEAREST);
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLSEARCH_H
