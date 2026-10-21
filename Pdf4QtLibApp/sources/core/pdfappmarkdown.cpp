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

#include "pdfappmarkdown.h"

#include <QUrl>

#include <array>
#include <map>

namespace pdf
{

/// Helpers of the parser of Markdown (lines of blocks, inline formats)
class PDFAppMarkdownParserHelper
{
public:
    PDFAppMarkdownParserHelper() = delete;

    /// Role of a character of an inline text
    enum Role : quint8
    {
        Normal,     ///< A character, which may be a delimiter
        Removed,    ///< A delimiter, which is not shown
        Literal,    ///< An escaped character or a delimiter without a pair
        Code        ///< Content of a code span
    };

    /// Longest run of backticks of a code span
    static constexpr qsizetype MAXIMUM_BACKTICKS = 32;

    /// Longest anchor
    static constexpr qsizetype MAXIMUM_ANCHOR_LENGTH = 64;

    static QString normalize(const QString& text, bool* truncated);
    static void parseBlocks(const QStringList& lines, int depth, std::vector<PDFAppMarkdownBlock>& blocks);

    static bool isBlank(const QString& line);
    static int getIndentation(const QString& line);
    static bool isFence(const QString& line, QString* fence);
    static bool isHeading(const QString& line, int* level, QString* text);
    static bool isRule(const QString& line);
    static bool isQuote(const QString& line);
    static bool isListMarker(const QString& line, bool* ordered, int* number, int* contentOffset, QChar* bullet);
    static bool isTableDelimiter(const QString& line, std::vector<int>* alignments);
    static bool isTableStart(const QStringList& lines, qsizetype index);
    static bool startsBlock(const QStringList& lines, qsizetype index, int depth);
    static QStringList splitTableRow(const QString& line);
    static void joinExtraCells(QStringList& cells, qsizetype columns);
    static QString stripQuoteMarker(const QString& line);
    static PDFAppMarkdownCallout getCallout(const QString& line, QString* title);
    static bool isImageParagraph(const QString& text, QString* alt, QString* source);
    static bool isAsciiPunctuation(QChar character);
    static bool isWhitespace(QChar character);

    static qsizetype parseFencedCode(const QStringList& lines, qsizetype index, std::vector<PDFAppMarkdownBlock>& blocks);
    static qsizetype parseQuote(const QStringList& lines, qsizetype index, int depth, std::vector<PDFAppMarkdownBlock>& blocks);
    static qsizetype parseList(const QStringList& lines, qsizetype index, int depth, std::vector<PDFAppMarkdownBlock>& blocks);
    static qsizetype parseTable(const QStringList& lines, qsizetype index, std::vector<PDFAppMarkdownBlock>& blocks);
    static qsizetype parseParagraph(const QStringList& lines, qsizetype index, int depth, std::vector<PDFAppMarkdownBlock>& blocks);

    static void assignAnchors(std::vector<PDFAppMarkdownBlock>& blocks, std::map<QString, int>& usedAnchors);
    static void collectHeadings(const std::vector<PDFAppMarkdownBlock>& blocks, std::vector<PDFAppMarkdownHeading>& headings);
    static void collectLinks(const std::vector<PDFAppMarkdownBlock>& blocks, QStringList& links);
    static void collectLinks(const PDFAppMarkdownSpans& spans, QStringList& links);
    static void collectText(const std::vector<PDFAppMarkdownBlock>& blocks, QStringList& texts);
    static QString sanitizeAnchor(const QString& text);
};

bool PDFAppMarkdownSpan::hasSameFormat(const PDFAppMarkdownSpan& other) const
{
    return bold == other.bold && italic == other.italic && code == other.code && link == other.link && linkKind == other.linkKind;
}

QString PDFAppMarkdownParserHelper::normalize(const QString& text, bool* truncated)
{
    QString result = text;
    *truncated = result.size() > PDFAppMarkdownParser::MAXIMUM_TEXT_LENGTH;
    if (*truncated)
    {
        result.truncate(PDFAppMarkdownParser::MAXIMUM_TEXT_LENGTH);
    }

    if (result.startsWith(QChar(0xFEFF)))
    {
        result.remove(0, 1);
    }

    QString normalized;
    normalized.reserve(result.size());
    const qsizetype count = result.size();
    for (qsizetype i = 0; i < count; ++i)
    {
        const QChar character = result.at(i);
        const char16_t code = character.unicode();
        if (code == u'\r')
        {
            normalized.append(QChar(u'\n'));
            if (i + 1 < count && result.at(i + 1) == QChar(u'\n'))
            {
                ++i;
            }
        }
        else if (code == u'\t')
        {
            normalized.append(QStringLiteral("    "));
        }
        else if (code == u'\n' || (code >= 0x20 && code != 0x7F && !(code >= 0x80 && code < 0xA0) &&
                                   code != 0x2028 && code != 0x2029 && code != 0xFEFF))
        {
            normalized.append(character);
        }
    }

    return normalized;
}

bool PDFAppMarkdownParserHelper::isBlank(const QString& line)
{
    for (const QChar character : line)
    {
        if (character != QChar(u' '))
        {
            return false;
        }
    }
    return true;
}

int PDFAppMarkdownParserHelper::getIndentation(const QString& line)
{
    int indentation = 0;
    while (indentation < line.size() && line.at(indentation) == QChar(u' '))
    {
        ++indentation;
    }
    return indentation;
}

bool PDFAppMarkdownParserHelper::isFence(const QString& line, QString* fence)
{
    const int indentation = getIndentation(line);
    if (indentation > 3 || indentation >= line.size())
    {
        return false;
    }

    const QChar character = line.at(indentation);
    if (character != QChar(u'`') && character != QChar(u'~'))
    {
        return false;
    }

    int length = 0;
    while (indentation + length < line.size() && line.at(indentation + length) == character)
    {
        ++length;
    }

    if (length < 3)
    {
        return false;
    }

    // The info string of a backtick fence cannot contain a backtick
    if (character == QChar(u'`') && line.indexOf(QChar(u'`'), indentation + length) >= 0)
    {
        return false;
    }

    if (fence)
    {
        *fence = QString(length, character);
    }
    return true;
}

bool PDFAppMarkdownParserHelper::isHeading(const QString& line, int* level, QString* text)
{
    const int indentation = getIndentation(line);
    if (indentation > 3)
    {
        return false;
    }

    int hashes = 0;
    while (indentation + hashes < line.size() && line.at(indentation + hashes) == QChar(u'#'))
    {
        ++hashes;
    }

    if (hashes < 1 || hashes > 6)
    {
        return false;
    }

    const int contentStart = indentation + hashes;
    if (contentStart < line.size() && line.at(contentStart) != QChar(u' '))
    {
        return false;
    }

    QString content = line.mid(contentStart).trimmed();

    // Closing sequence of hashes (preceded by a space, or the whole content)
    qsizetype end = content.size();
    while (end > 0 && content.at(end - 1) == QChar(u'#'))
    {
        --end;
    }
    if (end < content.size() && (end == 0 || content.at(end - 1) == QChar(u' ')))
    {
        content = content.left(end).trimmed();
    }

    if (level)
    {
        *level = hashes;
    }
    if (text)
    {
        *text = content;
    }
    return true;
}

bool PDFAppMarkdownParserHelper::isRule(const QString& line)
{
    if (getIndentation(line) > 3)
    {
        return false;
    }

    QChar character;
    int count = 0;
    for (const QChar current : line)
    {
        if (current == QChar(u' '))
        {
            continue;
        }

        if (current != QChar(u'-') && current != QChar(u'*') && current != QChar(u'_'))
        {
            return false;
        }

        if (count > 0 && current != character)
        {
            return false;
        }

        character = current;
        ++count;
    }
    return count >= 3;
}

bool PDFAppMarkdownParserHelper::isQuote(const QString& line)
{
    const int indentation = getIndentation(line);
    return indentation <= 3 && indentation < line.size() && line.at(indentation) == QChar(u'>');
}

QString PDFAppMarkdownParserHelper::stripQuoteMarker(const QString& line)
{
    const int indentation = getIndentation(line);
    if (indentation <= 3 && indentation < line.size() && line.at(indentation) == QChar(u'>'))
    {
        int start = indentation + 1;
        if (start < line.size() && line.at(start) == QChar(u' '))
        {
            ++start;
        }
        return line.mid(start);
    }
    return line;
}

bool PDFAppMarkdownParserHelper::isListMarker(const QString& line, bool* ordered, int* number, int* contentOffset, QChar* bullet)
{
    const int indentation = getIndentation(line);
    if (indentation > 3 || indentation >= line.size())
    {
        return false;
    }

    int position = indentation;
    const QChar first = line.at(position);
    bool isOrdered = false;
    int value = 0;

    if (first == QChar(u'-') || first == QChar(u'*') || first == QChar(u'+'))
    {
        ++position;
    }
    else if (first.isDigit() && first.unicode() < 128)
    {
        int digits = 0;
        while (position < line.size() && line.at(position).unicode() >= u'0' && line.at(position).unicode() <= u'9' && digits < 9)
        {
            value = value * 10 + (line.at(position).unicode() - u'0');
            ++position;
            ++digits;
        }

        if (position >= line.size() || (line.at(position) != QChar(u'.') && line.at(position) != QChar(u')')))
        {
            return false;
        }

        ++position;
        isOrdered = true;
    }
    else
    {
        return false;
    }

    // The marker is followed by a space or by the end of the line
    if (position < line.size() && line.at(position) != QChar(u' '))
    {
        return false;
    }

    int spaces = 0;
    while (position + spaces < line.size() && line.at(position + spaces) == QChar(u' ') && spaces < 5)
    {
        ++spaces;
    }

    int offset = position + 1;
    if (position + spaces < line.size() && spaces >= 1 && spaces <= 4)
    {
        offset = position + spaces;
    }

    if (ordered)
    {
        *ordered = isOrdered;
    }
    if (number)
    {
        *number = value;
    }
    if (contentOffset)
    {
        *contentOffset = offset;
    }
    if (bullet)
    {
        *bullet = isOrdered ? line.at(position - 1) : first;
    }
    return true;
}

QStringList PDFAppMarkdownParserHelper::splitTableRow(const QString& line)
{
    QString text = line.trimmed();
    if (text.startsWith(QChar(u'|')))
    {
        text.remove(0, 1);
    }
    if (text.endsWith(QChar(u'|')) && !text.endsWith(QStringLiteral("\\|")))
    {
        text.chop(1);
    }

    QStringList cells;
    QString cell;
    bool isInCode = false;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar character = text.at(i);
        if (character == QChar(u'\\') && i + 1 < text.size() && text.at(i + 1) == QChar(u'|'))
        {
            cell.append(QChar(u'|'));
            ++i;
            continue;
        }

        if (character == QChar(u'`'))
        {
            isInCode = !isInCode;
        }

        if (character == QChar(u'|') && !isInCode)
        {
            cells << cell.trimmed();
            cell.clear();
            continue;
        }
        cell.append(character);
    }
    cells << cell.trimmed();

    // Too many columns: the rest is joined into the last column
    joinExtraCells(cells, PDFAppMarkdownParser::MAXIMUM_TABLE_COLUMNS);
    return cells;
}

void PDFAppMarkdownParserHelper::joinExtraCells(QStringList& cells, qsizetype columns)
{
    if (columns > 0 && cells.size() > columns)
    {
        const QString rest = cells.mid(columns - 1).join(QStringLiteral(" | "));
        cells.erase(cells.begin() + (columns - 1), cells.end());
        cells << rest;
    }
}

bool PDFAppMarkdownParserHelper::isTableDelimiter(const QString& line, std::vector<int>* alignments)
{
    if (!line.contains(QChar(u'|')) && !line.contains(QChar(u'-')))
    {
        return false;
    }

    const QStringList cells = splitTableRow(line);
    std::vector<int> result;
    for (const QString& cell : cells)
    {
        if (cell.isEmpty())
        {
            return false;
        }

        const bool left = cell.startsWith(QChar(u':'));
        const bool right = cell.endsWith(QChar(u':'));
        const QString dashes = cell.mid(left ? 1 : 0, cell.size() - (left ? 1 : 0) - (right ? 1 : 0));
        if (dashes.isEmpty())
        {
            return false;
        }
        for (const QChar character : dashes)
        {
            if (character != QChar(u'-'))
            {
                return false;
            }
        }

        if (left && right)
        {
            result.push_back(Qt::AlignHCenter);
        }
        else if (right)
        {
            result.push_back(Qt::AlignRight);
        }
        else
        {
            result.push_back(Qt::AlignLeft);
        }
    }

    if (alignments)
    {
        *alignments = std::move(result);
    }
    return true;
}

bool PDFAppMarkdownParserHelper::isTableStart(const QStringList& lines, qsizetype index)
{
    if (index + 1 >= lines.size() || !lines.at(index).contains(QChar(u'|')) || getIndentation(lines.at(index)) > 3)
    {
        return false;
    }

    std::vector<int> alignments;
    if (!lines.at(index + 1).contains(QChar(u'|')) || !isTableDelimiter(lines.at(index + 1), &alignments))
    {
        return false;
    }

    return qsizetype(alignments.size()) == splitTableRow(lines.at(index)).size();
}

bool PDFAppMarkdownParserHelper::startsBlock(const QStringList& lines, qsizetype index, int depth)
{
    const QString& line = lines.at(index);
    if (isBlank(line) || isFence(line, nullptr) || isHeading(line, nullptr, nullptr) || isRule(line) || isTableStart(lines, index))
    {
        return true;
    }

    if (depth < PDFAppMarkdownParser::MAXIMUM_DEPTH)
    {
        if (isQuote(line))
        {
            return true;
        }

        int contentOffset = 0;
        if (isListMarker(line, nullptr, nullptr, &contentOffset, nullptr))
        {
            // An empty item does not interrupt a paragraph
            return !isBlank(line.mid(contentOffset));
        }
    }

    return false;
}

PDFAppMarkdownCallout PDFAppMarkdownParserHelper::getCallout(const QString& line, QString* title)
{
    const QString trimmed = line.trimmed();
    static constexpr std::pair<const char*, PDFAppMarkdownCallout> CALLOUTS[] = {
        { "[!NOTE]", PDFAppMarkdownCallout::Note },
        { "[!TIP]", PDFAppMarkdownCallout::Tip },
        { "[!IMPORTANT]", PDFAppMarkdownCallout::Important },
        { "[!WARNING]", PDFAppMarkdownCallout::Warning },
        { "[!CAUTION]", PDFAppMarkdownCallout::Caution }
    };

    for (const auto& [marker, callout] : CALLOUTS)
    {
        const QLatin1String markerText(marker);
        if (trimmed.startsWith(markerText, Qt::CaseInsensitive))
        {
            if (title)
            {
                *title = trimmed.mid(markerText.size()).trimmed();
            }
            return callout;
        }
    }

    return PDFAppMarkdownCallout::None;
}

bool PDFAppMarkdownParserHelper::isImageParagraph(const QString& text, QString* alt, QString* source)
{
    if (!text.startsWith(QStringLiteral("![")) || !text.endsWith(QChar(u')')) || text.size() > PDFAppMarkdownParser::MAXIMUM_LINK_LENGTH * 2)
    {
        return false;
    }

    const qsizetype altEnd = text.indexOf(QStringLiteral("]("));
    if (altEnd < 2)
    {
        return false;
    }

    const QString altText = text.mid(2, altEnd - 2);
    const QString target = text.mid(altEnd + 2, text.size() - altEnd - 3).trimmed();
    if (altText.contains(QChar(u'[')) || altText.contains(QChar(u']')) || target.isEmpty() || target.contains(QChar(u' ')) ||
        target.contains(QChar(u'(')) || target.contains(QChar(u')')))
    {
        return false;
    }

    *alt = altText;
    *source = target;
    return true;
}

bool PDFAppMarkdownParserHelper::isAsciiPunctuation(QChar character)
{
    const char16_t code = character.unicode();
    return (code >= 33 && code <= 47) || (code >= 58 && code <= 64) || (code >= 91 && code <= 96) || (code >= 123 && code <= 126);
}

bool PDFAppMarkdownParserHelper::isWhitespace(QChar character)
{
    return character.isSpace();
}

qsizetype PDFAppMarkdownParserHelper::parseFencedCode(const QStringList& lines, qsizetype index, std::vector<PDFAppMarkdownBlock>& blocks)
{
    QString fence;
    isFence(lines.at(index), &fence);
    const int indentation = getIndentation(lines.at(index));

    QStringList codeLines;
    qsizetype current = index + 1;
    while (current < lines.size())
    {
        const QString& line = lines.at(current);
        QString closingFence;
        if (isFence(line, &closingFence) && closingFence.at(0) == fence.at(0) && closingFence.size() >= fence.size() &&
            line.trimmed().size() == closingFence.size())
        {
            ++current;
            break;
        }

        // The indentation of the fence is removed from the lines of the code
        int remove = qMin(indentation, getIndentation(line));
        codeLines << line.mid(remove);
        ++current;
    }

    PDFAppMarkdownBlock block;
    block.type = PDFAppMarkdownBlock::Type::CodeBlock;
    block.text = codeLines.join(QChar(u'\n'));
    blocks.push_back(std::move(block));
    return current;
}

qsizetype PDFAppMarkdownParserHelper::parseQuote(const QStringList& lines, qsizetype index, int depth, std::vector<PDFAppMarkdownBlock>& blocks)
{
    QStringList content;
    qsizetype current = index;
    bool isPreviousParagraph = false;
    while (current < lines.size())
    {
        const QString& line = lines.at(current);
        if (isQuote(line))
        {
            const QString stripped = stripQuoteMarker(line);
            content << stripped;
            isPreviousParagraph = !isBlank(stripped);
            ++current;
            continue;
        }

        // Lazy continuation of a paragraph of the quote
        if (isPreviousParagraph && !isBlank(line) && !startsBlock(lines, current, depth))
        {
            content << line;
            ++current;
            continue;
        }
        break;
    }

    PDFAppMarkdownBlock block;
    block.type = PDFAppMarkdownBlock::Type::Quote;

    if (!content.isEmpty())
    {
        QString title;
        block.callout = getCallout(content.first(), &title);
        if (block.callout != PDFAppMarkdownCallout::None)
        {
            block.spans = PDFAppMarkdownParser::parseInline(title);
            content.removeFirst();
        }
    }

    parseBlocks(content, depth + 1, block.children);
    blocks.push_back(std::move(block));
    return current;
}

qsizetype PDFAppMarkdownParserHelper::parseList(const QStringList& lines, qsizetype index, int depth, std::vector<PDFAppMarkdownBlock>& blocks)
{
    PDFAppMarkdownBlock list;
    list.type = PDFAppMarkdownBlock::Type::List;

    QChar listBullet;
    bool isFirst = true;
    qsizetype current = index;

    while (current < lines.size())
    {
        bool ordered = false;
        int number = 0;
        int contentOffset = 0;
        QChar bullet;
        const QString& markerLine = lines.at(current);
        if (isRule(markerLine) || !isListMarker(markerLine, &ordered, &number, &contentOffset, &bullet))
        {
            break;
        }

        if (isFirst)
        {
            list.ordered = ordered;
            list.startNumber = number;
            listBullet = bullet;
            isFirst = false;
        }
        else if (ordered != list.ordered || bullet != listBullet)
        {
            // Another kind of marker starts a new list
            break;
        }

        QStringList itemLines;
        itemLines << markerLine.mid(contentOffset);
        ++current;

        int pendingBlanks = 0;
        bool isPreviousParagraph = !isBlank(itemLines.first());
        while (current < lines.size())
        {
            const QString& line = lines.at(current);
            if (isBlank(line))
            {
                ++pendingBlanks;
                ++current;
                continue;
            }

            if (getIndentation(line) >= contentOffset)
            {
                for (int i = 0; i < pendingBlanks; ++i)
                {
                    itemLines << QString();
                }
                pendingBlanks = 0;
                itemLines << line.mid(contentOffset);
                isPreviousParagraph = true;
                ++current;
                continue;
            }

            if (pendingBlanks == 0 && isPreviousParagraph && !startsBlock(lines, current, depth))
            {
                // Lazy continuation of the paragraph of the item
                itemLines << line.trimmed();
                ++current;
                continue;
            }

            break;
        }

        // Blank lines after the item are skipped: the next line is either the
        // marker of the next item (the loop continues), or another block
        PDFAppMarkdownBlock item;
        item.type = PDFAppMarkdownBlock::Type::ListItem;
        parseBlocks(itemLines, depth + 1, item.children);
        list.children.push_back(std::move(item));
    }

    blocks.push_back(std::move(list));
    return current;
}

qsizetype PDFAppMarkdownParserHelper::parseTable(const QStringList& lines, qsizetype index, std::vector<PDFAppMarkdownBlock>& blocks)
{
    PDFAppMarkdownBlock table;
    table.type = PDFAppMarkdownBlock::Type::Table;
    isTableDelimiter(lines.at(index + 1), &table.alignments);

    const qsizetype columns = qsizetype(table.alignments.size());
    std::vector<PDFAppMarkdownSpans> header;
    for (const QString& cell : splitTableRow(lines.at(index)))
    {
        header.push_back(PDFAppMarkdownParser::parseInline(cell));
    }
    table.rows.push_back(std::move(header));

    qsizetype current = index + 2;
    while (current < lines.size())
    {
        const QString& line = lines.at(current);
        if (isBlank(line) || !line.contains(QChar(u'|')) || isFence(line, nullptr) || isHeading(line, nullptr, nullptr) || isQuote(line))
        {
            break;
        }

        QStringList cells = splitTableRow(line);
        while (cells.size() < columns)
        {
            cells << QString();
        }
        joinExtraCells(cells, columns);

        std::vector<PDFAppMarkdownSpans> row;
        for (const QString& cell : cells)
        {
            row.push_back(PDFAppMarkdownParser::parseInline(cell));
        }
        table.rows.push_back(std::move(row));
        ++current;
    }

    blocks.push_back(std::move(table));
    return current;
}

qsizetype PDFAppMarkdownParserHelper::parseParagraph(const QStringList& lines, qsizetype index, int depth, std::vector<PDFAppMarkdownBlock>& blocks)
{
    QString text;
    qsizetype current = index;
    while (current < lines.size())
    {
        const QString& line = lines.at(current);
        if (current > index && startsBlock(lines, current, depth))
        {
            break;
        }

        if (current > index)
        {
            // Hard break: a backslash or two spaces at the end of the previous line
            if (text.endsWith(QChar(u'\\')))
            {
                text.chop(1);
                text.append(QChar(u'\n'));
            }
            else if (lines.at(current - 1).endsWith(QStringLiteral("  ")))
            {
                text.append(QChar(u'\n'));
            }
            else
            {
                text.append(QChar(u' '));
            }
        }

        text.append(line.trimmed());
        ++current;
    }

    // A paragraph, which is only a picture, is a block picture
    QString alt;
    QString source;
    PDFAppMarkdownBlock block;
    if (isImageParagraph(text, &alt, &source))
    {
        block.type = PDFAppMarkdownBlock::Type::Image;
        block.text = alt;
        block.source = source;
    }
    else
    {
        block.type = PDFAppMarkdownBlock::Type::Paragraph;
        block.spans = PDFAppMarkdownParser::parseInline(text);
    }

    blocks.push_back(std::move(block));
    return current;
}

void PDFAppMarkdownParserHelper::parseBlocks(const QStringList& lines, int depth, std::vector<PDFAppMarkdownBlock>& blocks)
{
    const bool isContainerAllowed = depth < PDFAppMarkdownParser::MAXIMUM_DEPTH;

    qsizetype index = 0;
    while (index < lines.size())
    {
        const QString& line = lines.at(index);
        int level = 0;
        QString text;

        if (isBlank(line))
        {
            ++index;
        }
        else if (isFence(line, nullptr))
        {
            index = parseFencedCode(lines, index, blocks);
        }
        else if (isHeading(line, &level, &text))
        {
            PDFAppMarkdownBlock block;
            block.type = PDFAppMarkdownBlock::Type::Heading;
            block.level = level;

            // Explicit anchor "{#anchor}" at the end of the heading
            if (text.endsWith(QChar(u'}')))
            {
                const qsizetype anchorStart = text.lastIndexOf(QStringLiteral("{#"));
                if (anchorStart >= 0)
                {
                    block.anchor = sanitizeAnchor(text.mid(anchorStart + 2, text.size() - anchorStart - 3));
                    text = text.left(anchorStart).trimmed();
                }
            }

            block.spans = PDFAppMarkdownParser::parseInline(text);
            blocks.push_back(std::move(block));
            ++index;
        }
        else if (isRule(line))
        {
            PDFAppMarkdownBlock block;
            block.type = PDFAppMarkdownBlock::Type::Rule;
            blocks.push_back(std::move(block));
            ++index;
        }
        else if (isContainerAllowed && isQuote(line))
        {
            index = parseQuote(lines, index, depth, blocks);
        }
        else if (isContainerAllowed && isListMarker(line, nullptr, nullptr, nullptr, nullptr))
        {
            index = parseList(lines, index, depth, blocks);
        }
        else if (isTableStart(lines, index))
        {
            index = parseTable(lines, index, blocks);
        }
        else
        {
            index = parseParagraph(lines, index, depth, blocks);
        }
    }
}

QString PDFAppMarkdownParserHelper::sanitizeAnchor(const QString& text)
{
    QString anchor;
    for (const QChar character : text)
    {
        if (anchor.size() >= MAXIMUM_ANCHOR_LENGTH)
        {
            break;
        }

        const char16_t code = character.unicode();
        if ((code >= u'a' && code <= u'z') || (code >= u'0' && code <= u'9') || code == u'-' || code == u'_')
        {
            anchor.append(character);
        }
        else if (code >= u'A' && code <= u'Z')
        {
            anchor.append(character.toLower());
        }
    }
    return anchor;
}

void PDFAppMarkdownParserHelper::assignAnchors(std::vector<PDFAppMarkdownBlock>& blocks, std::map<QString, int>& usedAnchors)
{
    for (PDFAppMarkdownBlock& block : blocks)
    {
        if (block.type == PDFAppMarkdownBlock::Type::Heading)
        {
            QString anchor = block.anchor.isEmpty() ? PDFAppMarkdownParser::createAnchor(PDFAppMarkdownParser::toPlainText(block.spans)) : block.anchor;
            if (anchor.isEmpty())
            {
                anchor = QStringLiteral("section");
            }

            // Duplicate anchors get a suffix ("usage", "usage-1", ...). The value of a
            // used anchor is the next suffix to try: the names below it are taken, so
            // each taken name is probed at most once (linear time also for thousands
            // of headings "x-1" ... "x-n" followed by thousands of headings "x")
            const int count = usedAnchors[anchor]++;
            if (count > 0)
            {
                QString unique;
                int suffix = count;
                do
                {
                    unique = anchor + QStringLiteral("-") + QString::number(suffix++);
                }
                while (usedAnchors.count(unique) > 0);
                usedAnchors[unique] = 1;
                usedAnchors[anchor] = suffix;
                anchor = unique;
            }
            block.anchor = anchor;
        }

        assignAnchors(block.children, usedAnchors);
    }
}

void PDFAppMarkdownParserHelper::collectHeadings(const std::vector<PDFAppMarkdownBlock>& blocks, std::vector<PDFAppMarkdownHeading>& headings)
{
    for (const PDFAppMarkdownBlock& block : blocks)
    {
        if (block.type == PDFAppMarkdownBlock::Type::Heading)
        {
            PDFAppMarkdownHeading heading;
            heading.level = block.level;
            heading.text = PDFAppMarkdownParser::toPlainText(block.spans);
            heading.anchor = block.anchor;
            headings.push_back(std::move(heading));
        }
        collectHeadings(block.children, headings);
    }
}

void PDFAppMarkdownParserHelper::collectLinks(const PDFAppMarkdownSpans& spans, QStringList& links)
{
    QString previous;
    for (const PDFAppMarkdownSpan& span : spans)
    {
        if (span.isLink() && span.link != previous)
        {
            links << span.link;
        }
        previous = span.isLink() ? span.link : QString();
    }
}

void PDFAppMarkdownParserHelper::collectLinks(const std::vector<PDFAppMarkdownBlock>& blocks, QStringList& links)
{
    for (const PDFAppMarkdownBlock& block : blocks)
    {
        collectLinks(block.spans, links);
        for (const std::vector<PDFAppMarkdownSpans>& row : block.rows)
        {
            for (const PDFAppMarkdownSpans& cell : row)
            {
                collectLinks(cell, links);
            }
        }
        collectLinks(block.children, links);
    }
}

void PDFAppMarkdownParserHelper::collectText(const std::vector<PDFAppMarkdownBlock>& blocks, QStringList& texts)
{
    for (const PDFAppMarkdownBlock& block : blocks)
    {
        switch (block.type)
        {
            case PDFAppMarkdownBlock::Type::Paragraph:
            case PDFAppMarkdownBlock::Type::Heading:
                texts << PDFAppMarkdownParser::toPlainText(block.spans);
                break;

            case PDFAppMarkdownBlock::Type::CodeBlock:
            case PDFAppMarkdownBlock::Type::Image:
                texts << block.text;
                break;

            case PDFAppMarkdownBlock::Type::Quote:
                if (!block.spans.empty())
                {
                    texts << PDFAppMarkdownParser::toPlainText(block.spans);
                }
                break;

            case PDFAppMarkdownBlock::Type::Table:
                for (const std::vector<PDFAppMarkdownSpans>& row : block.rows)
                {
                    QStringList cells;
                    for (const PDFAppMarkdownSpans& cell : row)
                    {
                        cells << PDFAppMarkdownParser::toPlainText(cell);
                    }
                    texts << cells.join(QChar(u'\t'));
                }
                break;

            case PDFAppMarkdownBlock::Type::List:
            case PDFAppMarkdownBlock::Type::ListItem:
            case PDFAppMarkdownBlock::Type::Rule:
                break;
        }

        collectText(block.children, texts);
    }
}

PDFAppMarkdownDocument PDFAppMarkdownParser::parse(const QString& text)
{
    PDFAppMarkdownDocument document;
    const QString normalized = PDFAppMarkdownParserHelper::normalize(text, &document.truncated);
    const QStringList lines = normalized.split(QChar(u'\n'));
    PDFAppMarkdownParserHelper::parseBlocks(lines, 0, document.blocks);

    std::map<QString, int> usedAnchors;
    PDFAppMarkdownParserHelper::assignAnchors(document.blocks, usedAnchors);
    return document;
}

PDFAppMarkdownSpans PDFAppMarkdownParser::parseInline(const QString& input)
{
    using Helper = PDFAppMarkdownParserHelper;

    PDFAppMarkdownSpans spans;
    const qsizetype count = input.size();
    if (count == 0)
    {
        return spans;
    }

    std::vector<quint8> roles(size_t(count), Helper::Normal);

    // 1. Runs of backticks (positions per length, for the closing run of a code span)
    std::map<qsizetype, std::vector<qsizetype>> backtickRuns;
    for (qsizetype i = 0; i < count;)
    {
        if (input.at(i) == QChar(u'`'))
        {
            qsizetype length = 0;
            while (i + length < count && input.at(i + length) == QChar(u'`'))
            {
                ++length;
            }
            backtickRuns[length].push_back(i);
            i += length;
        }
        else
        {
            ++i;
        }
    }
    std::map<qsizetype, size_t> backtickCursor;

    // 2. Escapes and code spans
    for (qsizetype i = 0; i < count;)
    {
        const QChar character = input.at(i);
        if (character == QChar(u'\\') && i + 1 < count && Helper::isAsciiPunctuation(input.at(i + 1)))
        {
            roles[size_t(i)] = Helper::Removed;
            roles[size_t(i + 1)] = Helper::Literal;
            i += 2;
            continue;
        }

        if (character == QChar(u'`'))
        {
            qsizetype length = 0;
            while (i + length < count && input.at(i + length) == QChar(u'`'))
            {
                ++length;
            }

            qsizetype closing = -1;
            if (length <= Helper::MAXIMUM_BACKTICKS)
            {
                const std::vector<qsizetype>& positions = backtickRuns[length];
                size_t& cursor = backtickCursor[length];
                while (cursor < positions.size() && positions[cursor] <= i)
                {
                    ++cursor;
                }
                if (cursor < positions.size())
                {
                    closing = positions[cursor];
                }
            }

            if (closing < 0)
            {
                for (qsizetype j = 0; j < length; ++j)
                {
                    roles[size_t(i + j)] = Helper::Literal;
                }
                i += length;
                continue;
            }

            for (qsizetype j = 0; j < length; ++j)
            {
                roles[size_t(i + j)] = Helper::Removed;
                roles[size_t(closing + j)] = Helper::Removed;
            }

            qsizetype contentStart = i + length;
            qsizetype contentEnd = closing;
            bool isOnlySpaces = true;
            for (qsizetype j = contentStart; j < contentEnd; ++j)
            {
                roles[size_t(j)] = Helper::Code;
                isOnlySpaces = isOnlySpaces && input.at(j) == QChar(u' ');
            }

            // One space on each side of the content is removed
            if (!isOnlySpaces && contentEnd - contentStart >= 2 && input.at(contentStart) == QChar(u' ') && input.at(contentEnd - 1) == QChar(u' '))
            {
                roles[size_t(contentStart)] = Helper::Removed;
                roles[size_t(contentEnd - 1)] = Helper::Removed;
            }

            i = closing + length;
            continue;
        }

        ++i;
    }

    // 3. Links: brackets matched in one pass, the next ')' and the next whitespace precomputed
    std::vector<qsizetype> bracketMatch(size_t(count), -1);
    {
        std::vector<qsizetype> stack;
        for (qsizetype i = 0; i < count; ++i)
        {
            if (roles[size_t(i)] != Helper::Normal)
            {
                continue;
            }

            if (input.at(i) == QChar(u'['))
            {
                stack.push_back(i);
            }
            else if (input.at(i) == QChar(u']') && !stack.empty())
            {
                bracketMatch[size_t(stack.back())] = i;
                stack.pop_back();
            }
        }
    }

    std::vector<qsizetype> nextParenthesis(size_t(count + 1), -1);
    std::vector<qsizetype> nextWhitespace(size_t(count + 1), count);
    for (qsizetype i = count - 1; i >= 0; --i)
    {
        nextParenthesis[size_t(i)] = (input.at(i) == QChar(u')') && roles[size_t(i)] == Helper::Normal) ? i : nextParenthesis[size_t(i + 1)];
        nextWhitespace[size_t(i)] = Helper::isWhitespace(input.at(i)) ? i : nextWhitespace[size_t(i + 1)];
    }

    struct LinkRange
    {
        qsizetype start = 0;
        qsizetype end = 0;
        QString target;
        PDFAppMarkdownLinkKind kind = PDFAppMarkdownLinkKind::None;
    };
    std::vector<LinkRange> links;
    std::vector<int> linkOfCharacter(size_t(count), -1);

    qsizetype linkEnd = 0;
    for (qsizetype i = 0; i < count; ++i)
    {
        if (i < linkEnd || roles[size_t(i)] != Helper::Normal || input.at(i) != QChar(u'['))
        {
            continue;
        }

        const qsizetype close = bracketMatch[size_t(i)];
        if (close < 0 || close + 1 >= count || input.at(close + 1) != QChar(u'(') || roles[size_t(close + 1)] != Helper::Normal)
        {
            continue;
        }

        const qsizetype targetStart = close + 2;
        const qsizetype parenthesis = targetStart <= count ? nextParenthesis[size_t(targetStart)] : -1;
        if (parenthesis < 0 || parenthesis - targetStart > MAXIMUM_LINK_LENGTH || nextWhitespace[size_t(targetStart)] < parenthesis)
        {
            continue;
        }

        const QString target = input.mid(targetStart, parenthesis - targetStart);
        const bool isImage = i > 0 && input.at(i - 1) == QChar(u'!') && roles[size_t(i - 1)] == Helper::Normal;

        roles[size_t(i)] = Helper::Removed;
        for (qsizetype j = close; j <= parenthesis; ++j)
        {
            roles[size_t(j)] = Helper::Removed;
        }

        if (isImage)
        {
            // A picture inside of a text is shown as its alternative text
            roles[size_t(i - 1)] = Helper::Removed;
        }
        else
        {
            LinkRange range;
            range.start = i + 1;
            range.end = close;
            range.target = target;
            range.kind = PDFAppMarkdownParser::classifyLink(target);
            if (range.kind != PDFAppMarkdownLinkKind::None)
            {
                const int linkIndex = int(links.size());
                for (qsizetype j = range.start; j < range.end; ++j)
                {
                    linkOfCharacter[size_t(j)] = linkIndex;
                }
                links.push_back(std::move(range));
            }
        }

        linkEnd = parenthesis + 1;
    }

    // 4. Emphasis: runs of * and _ paired by stacks per character and length (1 italic,
    //    2 bold, 3 both); pairs do not cross each other and the boundaries of links
    std::vector<int> boldDelta(size_t(count + 1), 0);
    std::vector<int> italicDelta(size_t(count + 1), 0);
    std::array<std::vector<qsizetype>, 6> openers;
    int currentLink = -2;

    for (qsizetype i = 0; i < count;)
    {
        const int link = linkOfCharacter[size_t(i)];
        if (link != currentLink)
        {
            for (std::vector<qsizetype>& stack : openers)
            {
                stack.clear();
            }
            currentLink = link;
        }

        const QChar character = input.at(i);
        if (roles[size_t(i)] != Helper::Normal || (character != QChar(u'*') && character != QChar(u'_')))
        {
            ++i;
            continue;
        }

        qsizetype length = 0;
        while (i + length < count && input.at(i + length) == character && roles[size_t(i + length)] == Helper::Normal &&
               linkOfCharacter[size_t(i + length)] == link)
        {
            ++length;
        }

        const QChar previous = i > 0 ? input.at(i - 1) : QChar(u' ');
        const QChar next = i + length < count ? input.at(i + length) : QChar(u' ');
        const bool isUnderscore = character == QChar(u'_');
        const bool canOpen = !Helper::isWhitespace(next) && !(isUnderscore && previous.isLetterOrNumber());
        const bool canClose = !Helper::isWhitespace(previous) && !(isUnderscore && next.isLetterOrNumber());

        if (length > 3)
        {
            for (qsizetype j = 0; j < length; ++j)
            {
                roles[size_t(i + j)] = Helper::Literal;
            }
            i += length;
            continue;
        }

        const size_t key = size_t((isUnderscore ? 3 : 0) + length - 1);
        if (canClose && !openers[key].empty())
        {
            const qsizetype opener = openers[key].back();
            openers[key].pop_back();

            for (qsizetype j = 0; j < length; ++j)
            {
                roles[size_t(opener + j)] = Helper::Removed;
                roles[size_t(i + j)] = Helper::Removed;
            }

            if (length >= 2)
            {
                ++boldDelta[size_t(opener + length)];
                --boldDelta[size_t(i)];
            }
            if (length != 2)
            {
                ++italicDelta[size_t(opener + length)];
                --italicDelta[size_t(i)];
            }

            // Openers inside of the pair cannot be closed later (no crossing pairs)
            for (std::vector<qsizetype>& stack : openers)
            {
                while (!stack.empty() && stack.back() > opener)
                {
                    stack.pop_back();
                }
            }
        }
        else if (canOpen)
        {
            openers[key].push_back(i);
        }

        i += length;
    }

    // 5. Spans of the characters, which are shown
    int bold = 0;
    int italic = 0;
    for (qsizetype i = 0; i < count; ++i)
    {
        bold += boldDelta[size_t(i)];
        italic += italicDelta[size_t(i)];

        const quint8 role = roles[size_t(i)];
        if (role == Helper::Removed)
        {
            continue;
        }

        PDFAppMarkdownSpan format;
        format.bold = bold > 0;
        format.italic = italic > 0;
        format.code = role == Helper::Code;

        const int link = linkOfCharacter[size_t(i)];
        if (link >= 0)
        {
            format.link = links[size_t(link)].target;
            format.linkKind = links[size_t(link)].kind;
        }

        if (spans.empty() || !spans.back().hasSameFormat(format))
        {
            spans.push_back(std::move(format));
        }
        spans.back().text.append(input.at(i));
    }

    return spans;
}

QString PDFAppMarkdownParser::createAnchor(const QString& text)
{
    QString anchor;
    bool isPendingDash = false;
    for (const QChar character : text)
    {
        if (anchor.size() >= PDFAppMarkdownParserHelper::MAXIMUM_ANCHOR_LENGTH)
        {
            break;
        }

        if (character.isLetterOrNumber() && character.unicode() < 128)
        {
            if (isPendingDash && !anchor.isEmpty())
            {
                anchor.append(QChar(u'-'));
            }
            isPendingDash = false;
            anchor.append(character.toLower());
        }
        else if (character == QChar(u' ') || character == QChar(u'-') || character == QChar(u'_'))
        {
            isPendingDash = true;
        }
    }

    while (anchor.size() > PDFAppMarkdownParserHelper::MAXIMUM_ANCHOR_LENGTH)
    {
        anchor.chop(1);
    }
    while (anchor.endsWith(QChar(u'-')))
    {
        anchor.chop(1);
    }
    return anchor;
}

PDFAppMarkdownLinkKind PDFAppMarkdownParser::classifyLink(const QString& target)
{
    if (target.isEmpty() || target.size() > MAXIMUM_LINK_LENGTH)
    {
        return PDFAppMarkdownLinkKind::None;
    }

    for (const QChar character : target)
    {
        if (character.unicode() < 0x21 || character.unicode() == 0x7F)
        {
            return PDFAppMarkdownLinkKind::None;
        }
    }

    if (target.startsWith(QStringLiteral("https://")))
    {
        const QUrl url(target, QUrl::StrictMode);
        if (url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty() && url.userInfo().isEmpty())
        {
            return PDFAppMarkdownLinkKind::External;
        }
        return PDFAppMarkdownLinkKind::None;
    }

    if (target.startsWith(QChar(u'/')) && !target.startsWith(QStringLiteral("//")))
    {
        return PDFAppMarkdownLinkKind::Route;
    }

    if (target.startsWith(QChar(u'#')) && target.size() > 1 && PDFAppMarkdownParserHelper::sanitizeAnchor(target.mid(1)) == target.mid(1))
    {
        return PDFAppMarkdownLinkKind::Anchor;
    }

    return PDFAppMarkdownLinkKind::None;
}

QString PDFAppMarkdownParser::toPlainText(const PDFAppMarkdownSpans& spans)
{
    QString text;
    for (const PDFAppMarkdownSpan& span : spans)
    {
        text += span.text;
    }
    return text;
}

std::vector<PDFAppMarkdownHeading> PDFAppMarkdownDocument::getHeadings() const
{
    std::vector<PDFAppMarkdownHeading> headings;
    PDFAppMarkdownParserHelper::collectHeadings(blocks, headings);
    return headings;
}

bool PDFAppMarkdownDocument::hasAnchor(const QString& anchor) const
{
    for (const PDFAppMarkdownHeading& heading : getHeadings())
    {
        if (heading.anchor == anchor)
        {
            return true;
        }
    }
    return false;
}

QStringList PDFAppMarkdownDocument::getLinks() const
{
    QStringList links;
    PDFAppMarkdownParserHelper::collectLinks(blocks, links);
    return links;
}

QString PDFAppMarkdownDocument::toPlainText() const
{
    QStringList texts;
    PDFAppMarkdownParserHelper::collectText(blocks, texts);
    return texts.join(QChar(u'\n'));
}

}   // namespace pdf
