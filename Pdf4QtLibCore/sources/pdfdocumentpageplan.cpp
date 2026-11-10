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

#include "pdfdocumentpageplan.h"

#include "pdfdocumenttoolparameter.h"

#include <QStringList>

#include <algorithm>
#include <limits>
#include <set>

namespace pdf
{

/// Helpers of the plan of the pages
class PDFDocumentPagePlanHelper
{
public:
    PDFDocumentPagePlanHelper() = delete;

    /// One parsed item of a text of a plan
    struct Token
    {
        int input = -1;
        PDFInteger firstPage = 0;   ///< Page number from 1
        PDFInteger lastPage = 0;    ///< Page number from 1
        PageRotation turn = PageRotation::None;
        bool isClone = false;
    };

    /// Reads a decimal number of at most 10 digits at \p position
    static bool readNumber(QStringView text, qsizetype* position, PDFInteger* value)
    {
        qsizetype end = *position;
        while (end < text.size() && text[end] >= QLatin1Char('0') && text[end] <= QLatin1Char('9') && end - *position < 10)
        {
            ++end;
        }
        if (end == *position || (end < text.size() && text[end].isDigit()))
        {
            return false;
        }

        bool ok = false;
        *value = text.mid(*position, end - *position).toLongLong(&ok);
        *position = end;
        return ok;
    }

    /// Parses "<letters><page>[-<page>][@<turn>][+]"
    static std::optional<Token> parseToken(QStringView text)
    {
        Token token;
        qsizetype position = 0;
        while (position < text.size() && text[position] >= QLatin1Char('A') && text[position] <= QLatin1Char('Z'))
        {
            ++position;
        }
        if (position == 0 || position > PDFDocumentPagePlan::MAXIMUM_LETTERS)
        {
            return std::nullopt;
        }
        token.input = PDFDocumentPagePlan::getInputIndex(text.left(position));

        if (!readNumber(text, &position, &token.firstPage))
        {
            return std::nullopt;
        }
        token.lastPage = token.firstPage;

        if (position < text.size() && text[position] == QLatin1Char('-'))
        {
            ++position;
            if (!readNumber(text, &position, &token.lastPage))
            {
                return std::nullopt;
            }
        }

        if (position < text.size() && text[position] == QLatin1Char('@'))
        {
            ++position;
            PDFInteger angle = 0;
            if (!readNumber(text, &position, &angle))
            {
                return std::nullopt;
            }
            switch (angle)
            {
                case 0:
                    token.turn = PageRotation::None;
                    break;
                case 90:
                    token.turn = PageRotation::Rotate90;
                    break;
                case 180:
                    token.turn = PageRotation::Rotate180;
                    break;
                case 270:
                    token.turn = PageRotation::Rotate270;
                    break;
                default:
                    return std::nullopt;
            }
        }

        if (position < text.size() && text[position] == QLatin1Char('+'))
        {
            token.isClone = true;
            ++position;
        }

        if (position != text.size())
        {
            return std::nullopt;
        }
        return token;
    }

    /// Angle of a turn in a plan
    static int getAngle(PageRotation turn)
    {
        switch (turn)
        {
            case PageRotation::Rotate90:
                return 90;
            case PageRotation::Rotate180:
                return 180;
            case PageRotation::Rotate270:
                return 270;
            default:
                return 0;
        }
    }

    /// Text of an item of a plan
    static QString createToken(int input, PDFInteger firstPage, PDFInteger lastPage, PageRotation turn, bool isClone)
    {
        QString text = PDFDocumentPagePlan::getInputLetters(input) + QString::number(firstPage + 1);
        if (lastPage != firstPage)
        {
            text += QLatin1Char('-') + QString::number(lastPage + 1);
        }
        if (turn != PageRotation::None)
        {
            text += QLatin1Char('@') + QString::number(getAngle(turn));
        }
        if (isClone)
        {
            text += QLatin1Char('+');
        }
        return text;
    }

    /// An item of a page of an input
    static PDFDocumentPagePlan::Item createItem(int input, PDFInteger pageIndex)
    {
        PDFDocumentPagePlan::Item item;
        item.input = input;
        item.pageIndex = pageIndex;
        return item;
    }
};

QString PDFDocumentPagePlan::getInputLetters(int inputIndex)
{
    // Bijective base 26: A ... Z, AA, AB, ... (R20 of step 2)
    QString letters;
    if (inputIndex < 0)
    {
        return letters;
    }

    qint64 value = qint64(inputIndex) + 1;
    while (value > 0)
    {
        const qint64 remainder = (value - 1) % 26;
        letters.prepend(QLatin1Char(char('A' + remainder)));
        value = (value - 1) / 26;
    }
    return letters;
}

int PDFDocumentPagePlan::getInputIndex(QStringView letters)
{
    if (letters.isEmpty() || letters.size() > MAXIMUM_LETTERS)
    {
        return -1;
    }

    qint64 value = 0;
    for (const QChar character : letters)
    {
        if (character < QLatin1Char('A') || character > QLatin1Char('Z'))
        {
            return -1;
        }
        value = value * 26 + (character.unicode() - 'A' + 1);
    }
    return value - 1 <= std::numeric_limits<int>::max() ? int(value - 1) : -1;
}

std::optional<std::vector<PDFDocumentPagePlan::Item>> PDFDocumentPagePlan::parse(const QString& text, const std::vector<PDFInteger>& pageCounts, QString* errorMessage)
{
    using Helper = PDFDocumentPagePlanHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (text.size() > MAXIMUM_TEXT_LENGTH)
    {
        error = tr("The plan of the pages is longer than %1 characters.").arg(MAXIMUM_TEXT_LENGTH);
        return std::nullopt;
    }

    std::vector<Item> items;
    std::set<std::pair<int, PDFInteger>> usedPages;
    const QStringView planView(text);
    qsizetype start = 0;
    while (start <= planView.size())
    {
        qsizetype end = planView.indexOf(QLatin1Char(','), start);
        if (end < 0)
        {
            end = planView.size();
        }

        const QStringView part = planView.mid(start, end - start).trimmed();
        start = end + 1;
        if (part.isEmpty())
        {
            continue;
        }

        const std::optional<Helper::Token> token = Helper::parseToken(part);
        if (!token)
        {
            error = tr("“%1” is not a part of a plan of the pages. Write the file and its pages, such as A1-6, B3@90 or A3+ (a turn @90, @180, @270; + a copy of a page).")
                        .arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(part.toString()));
            return std::nullopt;
        }
        if (token->input < 0 || size_t(token->input) >= pageCounts.size())
        {
            error = tr("The plan of the pages names the file %1, which is not in the list.").arg(PDFDocumentToolParameterDefinition::sanitizeForMessage(part.toString()));
            return std::nullopt;
        }

        const PDFInteger pageCount = pageCounts[size_t(token->input)];
        if (token->firstPage < 1 || token->lastPage < 1 || token->firstPage > pageCount || token->lastPage > pageCount)
        {
            error = tr("The file %1 has no page %2 (it has %Ln pages).", nullptr, int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max())))
                        .arg(getInputLetters(token->input))
                        .arg(token->firstPage > pageCount || token->firstPage < 1 ? token->firstPage : token->lastPage);
            return std::nullopt;
        }

        const PDFInteger step = token->lastPage >= token->firstPage ? 1 : -1;
        const PDFInteger count = (token->lastPage - token->firstPage) * step + 1;
        if (PDFInteger(items.size()) + count > PDFDocumentMerger::MAXIMUM_PAGE_COUNT)
        {
            error = tr("The plan of the pages has more than %Ln pages.", nullptr, int(PDFDocumentMerger::MAXIMUM_PAGE_COUNT));
            return std::nullopt;
        }

        for (PDFInteger page = token->firstPage - 1; ; page += step)
        {
            Item item = Helper::createItem(token->input, page);
            item.turn = token->turn;
            item.isClone = !usedPages.emplace(token->input, page).second;
            if (item.isClone && !token->isClone)
            {
                error = tr("The page %1 of the file %2 is in the plan of the pages more than once. Mark each further copy of it by +, such as %2%1+.")
                            .arg(page + 1)
                            .arg(getInputLetters(token->input));
                return std::nullopt;
            }
            items.push_back(std::move(item));
            if (page == token->lastPage - 1)
            {
                break;
            }
        }
    }

    if (items.empty())
    {
        error = tr("The plan of the pages has no page.");
        return std::nullopt;
    }
    return items;
}

QString PDFDocumentPagePlan::toText(const std::vector<Item>& items)
{
    QStringList parts;
    for (size_t i = 0; i < items.size();)
    {
        const Item& first = items[i];
        size_t last = i;
        PDFInteger step = 0;
        if (i + 1 < items.size())
        {
            const Item& next = items[i + 1];
            const PDFInteger difference = next.pageIndex - first.pageIndex;
            if (next.input == first.input && next.turn == first.turn && next.isClone == first.isClone && (difference == 1 || difference == -1))
            {
                step = difference;
            }
        }
        if (step != 0)
        {
            while (last + 1 < items.size() &&
                   items[last + 1].input == first.input &&
                   items[last + 1].turn == first.turn &&
                   items[last + 1].isClone == first.isClone &&
                   items[last + 1].pageIndex == items[last].pageIndex + step)
            {
                ++last;
            }
        }
        parts << PDFDocumentPagePlanHelper::createToken(first.input, first.pageIndex, items[last].pageIndex, first.turn, first.isClone);
        i = last + 1;
    }
    return parts.join(QLatin1Char(','));
}

std::vector<PDFDocumentPagePlan::Item> PDFDocumentPagePlan::createDefault(const std::vector<PDFInteger>& pageCounts)
{
    std::vector<Item> items;
    for (size_t input = 0; input < pageCounts.size(); ++input)
    {
        for (PDFInteger page = 0; page < pageCounts[input]; ++page)
        {
            items.push_back(PDFDocumentPagePlanHelper::createItem(int(input), page));
        }
    }
    return items;
}

std::vector<PDFDocumentPagePlan::Item> PDFDocumentPagePlan::interleave(PDFInteger frontPageCount, PDFInteger backPageCount, const InterleaveSettings& settings)
{
    using Helper = PDFDocumentPagePlanHelper;

    // The sides in the order of reading: the back sides of a scan of a stack turned over come
    // from the last page to the first (385:13263: front 1, back 6, front 2, back 5, ...)
    frontPageCount = qMax<PDFInteger>(frontPageCount, 0);
    backPageCount = qMax<PDFInteger>(backPageCount, 0);
    auto getBackPage = [&settings, backPageCount](PDFInteger index)
    {
        return settings.isBackReversed ? backPageCount - 1 - index : index;
    };

    std::vector<Item> items;
    items.reserve(size_t(frontPageCount + backPageCount));
    const PDFInteger pairs = qMin(frontPageCount, backPageCount);
    for (PDFInteger i = 0; i < pairs; ++i)
    {
        items.push_back(Helper::createItem(0, i));
        items.push_back(Helper::createItem(1, getBackPage(i)));
    }

    // The sides without a pair (385:14487: front 6, front 7 at the end), in their order
    if (settings.areExtrasKept)
    {
        for (PDFInteger i = pairs; i < frontPageCount; ++i)
        {
            items.push_back(Helper::createItem(0, i));
        }
        for (PDFInteger i = pairs; i < backPageCount; ++i)
        {
            items.push_back(Helper::createItem(1, getBackPage(i)));
        }
    }
    return items;
}

std::vector<PDFDocumentPagePlan::Item> PDFDocumentPagePlan::reverse(std::vector<Item> items)
{
    std::reverse(items.begin(), items.end());

    // The first occurrence of a page is the page itself, the others are its clones
    std::set<std::pair<int, PDFInteger>> usedPages;
    for (Item& item : items)
    {
        item.isClone = !usedPages.emplace(item.input, item.pageIndex).second;
    }
    return items;
}

std::vector<PDFDocumentMerger::Item> PDFDocumentPagePlan::createMergerItems(const std::vector<Item>& items)
{
    std::vector<PDFDocumentMerger::Item> mergerItems;
    mergerItems.reserve(items.size());
    for (const Item& planItem : items)
    {
        PDFDocumentMerger::Item item;
        item.source = size_t(qMax(planItem.input, 0));
        item.pageIndex = planItem.pageIndex;
        item.turn = planItem.turn;
        item.pageSize = planItem.pageSize;
        item.imageLayout = planItem.imageLayout;
        mergerItems.push_back(std::move(item));
    }
    return mergerItems;
}

}   // namespace pdf
