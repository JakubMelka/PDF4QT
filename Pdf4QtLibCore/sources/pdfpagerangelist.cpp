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

#include "pdfpagerangelist.h"

#include <QStringList>

#include <limits>

namespace pdf
{

/// Helpers of the parser of the list
class PDFPageRangeListHelper
{
public:
    PDFPageRangeListHelper() = delete;

    /// Most digits of a number (no overflow of PDFInteger)
    static constexpr int MAXIMUM_DIGITS = 10;

    /// Reads a page number of ASCII digits; -1 for an empty text, -2 for an invalid one
    static PDFInteger readNumber(QStringView text)
    {
        if (text.isEmpty())
        {
            return -1;
        }
        if (text.size() > MAXIMUM_DIGITS)
        {
            return -2;
        }

        PDFInteger value = 0;
        for (const QChar character : text)
        {
            if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            {
                return -2;
            }
            value = value * 10 + (character.unicode() - u'0');
        }
        return value;
    }
};

std::optional<PDFPageRangeList> PDFPageRangeList::parse(const QString& text, PDFInteger pageCount, QString* errorMessage)
{
    using Helper = PDFPageRangeListHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (text.size() > MAXIMUM_TEXT_LENGTH)
    {
        error = tr("The list of pages is too long.");
        return std::nullopt;
    }

    PDFPageRangeList list;
    const QStringList items = text.split(QLatin1Char(','));
    if (text.trimmed().isEmpty())
    {
        return list;
    }

    for (const QString& rawItem : items)
    {
        const QString item = rawItem.trimmed();
        PDFInteger first = -1;
        PDFInteger last = -1;
        PDFInteger step = 1;

        if (item == QLatin1String("odd") || item == QLatin1String("even"))
        {
            first = item == QLatin1String("odd") ? 1 : 2;
            last = pageCount;
            step = 2;
            if (first > last)
            {
                // A document of one page has no even page: nothing to add
                continue;
            }
        }
        else
        {
            const qsizetype dash = item.indexOf(QLatin1Char('-'));
            if (dash < 0)
            {
                first = Helper::readNumber(item);
                last = first;
            }
            else
            {
                first = Helper::readNumber(QStringView(item).left(dash).trimmed());
                last = Helper::readNumber(QStringView(item).mid(dash + 1).trimmed());
                if (first == -1 && last == -1)
                {
                    first = -2;
                }
                else if (first == -1)
                {
                    first = 1;
                }
                else if (last == -1)
                {
                    last = pageCount;
                }
            }

            if (first == -1 || first == -2 || last == -2)
            {
                error = tr("\"%1\" is not a page or a range of pages. Examples: 1-5, 7, 10-, odd, even").arg(item);
                return std::nullopt;
            }
            for (const PDFInteger page : { first, last })
            {
                if (page < 1 || page > pageCount)
                {
                    error = tr("Page %1 does not exist – the document has %Ln pages", nullptr, int(qMin<PDFInteger>(pageCount, std::numeric_limits<int>::max()))).arg(page);
                    return std::nullopt;
                }
            }
            if (first > last)
            {
                step = -1;
            }
        }

        const PDFInteger count = (step > 0 ? last - first : first - last) / qAbs(step) + 1;
        if (list.getPageCount() + count > MAXIMUM_PAGE_COUNT)
        {
            error = tr("The list has more than %Ln pages.", nullptr, int(MAXIMUM_PAGE_COUNT));
            return std::nullopt;
        }
        for (PDFInteger i = 0, page = first; i < count; ++i, page += step)
        {
            list.m_pageIndices.push_back(page - 1);
        }
    }

    return list;
}

QString PDFPageRangeList::toText() const
{
    QStringList items;
    size_t i = 0;
    while (i < m_pageIndices.size())
    {
        // The longest run of consecutive pages in one direction
        size_t end = i + 1;
        PDFInteger direction = 0;
        if (end < m_pageIndices.size())
        {
            const PDFInteger difference = m_pageIndices[end] - m_pageIndices[i];
            if (difference == 1 || difference == -1)
            {
                direction = difference;
                while (end < m_pageIndices.size() && m_pageIndices[end] - m_pageIndices[end - 1] == direction)
                {
                    ++end;
                }
            }
        }

        const PDFInteger first = m_pageIndices[i] + 1;
        const PDFInteger last = m_pageIndices[end - 1] + 1;
        items << (first == last ? QString::number(first) : QStringLiteral("%1-%2").arg(first).arg(last));
        i = end;
    }
    return items.join(QStringLiteral(", "));
}

}   // namespace pdf
