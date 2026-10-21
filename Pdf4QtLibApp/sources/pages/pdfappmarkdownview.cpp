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

#include "pdfappmarkdownview.h"

#include "pdfaccenthue.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QFile>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QStyle>
#include <QTextLayout>

#include <cmath>

namespace pdf
{

/// Laid out content of a Markdown view for one width: the shapes (cards of code,
/// callouts, tables, dividers), the texts (QTextLayout with explicit formats of the
/// tokens), the icons, the pictures, the rectangles of the links and the positions
/// of the anchors. Coordinates are the coordinates of the view.
class PDFAppMarkdownLayout
{
public:
    struct Shape
    {
        QRectF rect;
        PDFStylePainter::Radii radii;
        QColor fill;
        QColor border;
    };

    struct Text
    {
        std::unique_ptr<QTextLayout> layout;
        QPointF position;
        QRectF bounds;
    };

    struct Icon
    {
        QRectF rect;
        QString name;
        QColor color;
    };

    struct Picture
    {
        QRectF rect;
        QImage image;
    };

    struct LinkArea
    {
        int link = -1;
        int text = -1;
        QRectF rect;
        qreal baseline = 0.0;
    };

    int width = -1;
    qreal height = 0.0;
    std::vector<Shape> shapes;
    std::vector<std::unique_ptr<Text>> texts;
    std::vector<Icon> icons;
    std::vector<Picture> pictures;
    std::vector<LinkArea> linkAreas;
    std::vector<std::pair<QString, qreal>> anchors;
    std::vector<QString> linkTargets;
    std::vector<PDFAppMarkdownLinkKind> linkKinds;
};

/// Builds a PDFAppMarkdownLayout (the typography of Figma pdf4qt-help 955:152873)
class PDFAppMarkdownLayoutBuilder
{
public:
    /// Typography of a text of a block
    struct TextStyle
    {
        int pixelSize = 13;
        int lineHeight = 20;
        QFont::Weight weight = QFont::Normal;
        PDFThemeColors::Color color = PDFThemeColors::TextSecondary;
        bool italic = false;
        bool isCode = false;
        bool wrapAnywhere = false;  ///< Wrapped anywhere (code), not at the word boundaries
    };

    /// Context of nested blocks
    struct Context
    {
        TextStyle body;
        qreal blockSpacing = 14.0;
        int depth = 0;
    };

    PDFAppMarkdownLayoutBuilder(PDFAppMarkdownLayout* layout, const QString& resourceBase, Qt::LayoutDirection direction) :
        m_layout(layout),
        m_resourceBase(resourceBase),
        m_direction(direction)
    {

    }

    /// Lays the blocks out from \p y, returns the bottom
    qreal layoutBlocks(const std::vector<PDFAppMarkdownBlock>& blocks, qreal x, qreal width, qreal y, const Context& context);

    /// Mirrors everything horizontally for the right-to-left layout
    void mirror(qreal totalWidth);
    static void mirrorRect(QRectF& rect, qreal totalWidth);

    static constexpr qreal LIST_INDENT = 4.0;
    static constexpr qreal MARKER_WIDTH = 18.0;
    static constexpr qreal MARKER_GAP = 10.0;
    static constexpr qreal LIST_ITEM_SPACING = 6.0;
    static constexpr qreal MINIMUM_WIDTH = 40.0;
    static constexpr int MAXIMUM_PICTURE_SIZE = 4096;

    /// Longest text and most spans of one QTextLayout
    static constexpr qsizetype MAXIMUM_CHUNK_LENGTH = 4096;
    static constexpr qsizetype MAXIMUM_CHUNK_SPANS = 256;

    static QFont createFont(const TextStyle& style, bool bold);

private:
    qreal layoutBlock(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y, const Context& context);
    qreal addText(const PDFAppMarkdownSpans& spans, const TextStyle& style, qreal x, qreal width, qreal y, Qt::Alignment alignment = Qt::Alignment());
    qreal addTextChunk(const PDFAppMarkdownSpans& spans, const TextStyle& style, qreal x, qreal width, qreal y, Qt::Alignment alignment);
    qreal addPlainText(const QString& text, const TextStyle& style, qreal x, qreal width, qreal y, Qt::Alignment alignment = Qt::Alignment(), bool wrapAnywhere = false);
    qreal layoutList(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y, const Context& context);
    qreal layoutQuote(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y, const Context& context);
    qreal layoutTable(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y);
    qreal layoutPicture(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y, const Context& context);
    QImage loadPicture(const QString& source) const;
    qreal measureNatural(const PDFAppMarkdownSpans& spans, const TextStyle& style) const;
    int getLinkIndex(const QString& target, PDFAppMarkdownLinkKind kind, const void* owner);

    PDFAppMarkdownLayout* m_layout;
    QString m_resourceBase;
    Qt::LayoutDirection m_direction;

    /// The last link (its target and the text, which holds it), so the spans of one
    /// link with different formats are one link
    const void* m_lastLinkOwner = nullptr;
    QString m_lastLinkTarget;
    int m_lastLinkIndex = -1;
};

QFont PDFAppMarkdownLayoutBuilder::createFont(const TextStyle& style, bool bold)
{
    QFont font;
    if (style.isCode)
    {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPixelSize(qMax(style.pixelSize - 1, 8));
        font.setBold(bold);
    }
    else
    {
        QFont::Weight weight = style.weight;
        if (bold && weight < QFont::DemiBold)
        {
            weight = QFont::DemiBold;
        }
        font = PDFThemeFonts::getFont(weight, style.pixelSize);
    }
    font.setItalic(style.italic);
    return font;
}

int PDFAppMarkdownLayoutBuilder::getLinkIndex(const QString& target, PDFAppMarkdownLinkKind kind, const void* owner)
{
    if (owner == m_lastLinkOwner && target == m_lastLinkTarget && m_lastLinkIndex >= 0)
    {
        return m_lastLinkIndex;
    }

    m_lastLinkOwner = owner;
    m_lastLinkTarget = target;
    m_lastLinkIndex = int(m_layout->linkTargets.size());
    m_layout->linkTargets.push_back(target);
    m_layout->linkKinds.push_back(kind);
    return m_lastLinkIndex;
}

qreal PDFAppMarkdownLayoutBuilder::addText(const PDFAppMarkdownSpans& spans, const TextStyle& style, qreal x, qreal width, qreal y, Qt::Alignment alignment)
{
    // A very long text (hostile input) is laid out in chunks: the time of QTextLayout
    // grows faster than the length of its text and the number of its formats
    qsizetype length = 0;
    for (const PDFAppMarkdownSpan& span : spans)
    {
        length += span.text.size();
    }

    if (length <= MAXIMUM_CHUNK_LENGTH && qsizetype(spans.size()) <= MAXIMUM_CHUNK_SPANS)
    {
        return addTextChunk(spans, style, x, width, y, alignment);
    }

    PDFAppMarkdownSpans chunk;
    qsizetype chunkLength = 0;
    for (const PDFAppMarkdownSpan& span : spans)
    {
        qsizetype offset = 0;
        while (offset < span.text.size())
        {
            qsizetype count = qMin(span.text.size() - offset, MAXIMUM_CHUNK_LENGTH - chunkLength);
            if (count > 1 && offset + count < span.text.size() && span.text.at(offset + count - 1).isHighSurrogate())
            {
                // A surrogate pair is not cut into two chunks
                --count;
            }
            PDFAppMarkdownSpan part = span;
            part.text = span.text.mid(offset, count);
            chunk.push_back(std::move(part));
            chunkLength += count;
            offset += count;

            if (chunkLength >= MAXIMUM_CHUNK_LENGTH || qsizetype(chunk.size()) >= MAXIMUM_CHUNK_SPANS)
            {
                y = addTextChunk(chunk, style, x, width, y, alignment);
                chunk.clear();
                chunkLength = 0;
            }
        }
    }

    if (!chunk.empty())
    {
        y = addTextChunk(chunk, style, x, width, y, alignment);
    }
    return y;
}

qreal PDFAppMarkdownLayoutBuilder::addTextChunk(const PDFAppMarkdownSpans& spans, const TextStyle& style, qreal x, qreal width, qreal y, Qt::Alignment alignment)
{
    width = qMax(width, MINIMUM_WIDTH);

    std::unique_ptr<PDFAppMarkdownLayout::Text> item = std::make_unique<PDFAppMarkdownLayout::Text>();
    item->layout = std::make_unique<QTextLayout>();
    QTextLayout* layout = item->layout.get();

    QString text;
    QList<QTextLayout::FormatRange> formats;
    struct LinkRange
    {
        int start = 0;
        int length = 0;
        int link = -1;
    };
    std::vector<LinkRange> linkRanges;

    // A new text never continues the link of the previous one
    m_lastLinkOwner = layout;

    for (const PDFAppMarkdownSpan& span : spans)
    {
        QString spanText = span.text;
        spanText.replace(QChar(u'\n'), QChar::LineSeparator);
        if (spanText.isEmpty())
        {
            continue;
        }

        TextStyle spanStyle = style;
        spanStyle.italic = style.italic || span.italic;
        spanStyle.isCode = style.isCode || span.code;

        QTextLayout::FormatRange range;
        range.start = int(text.size());
        range.length = int(spanText.size());
        range.format.setFont(createFont(spanStyle, span.bold));

        PDFThemeColors::Color color = span.bold && style.color == PDFThemeColors::TextSecondary ? PDFThemeColors::TextPrimary : style.color;
        if (span.isLink())
        {
            color = PDFThemeColors::TextAccent;
            linkRanges.push_back({ range.start, range.length, getLinkIndex(span.link, span.linkKind, layout) });
        }
        else
        {
            m_lastLinkOwner = nullptr;
        }

        range.format.setForeground(PDFStylePainter::color(color));
        if (span.code && !style.isCode)
        {
            range.format.setBackground(PDFStylePainter::color(PDFThemeColors::BgSubtle));
        }

        formats.push_back(range);
        text += spanText;
    }

    if (text.isEmpty())
    {
        return y;
    }

    QTextOption option;
    option.setWrapMode(style.wrapAnywhere ? QTextOption::WrapAnywhere : QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setTextDirection(m_direction);
    // QTextLayout takes AlignLeading / AlignTrailing as AlignLeft / AlignRight, the
    // alignment is resolved by the direction of the view (right-to-left: to the right)
    option.setAlignment(QStyle::visualAlignment(m_direction, alignment ? alignment : Qt::Alignment(Qt::AlignLeading)));
    layout->setTextOption(option);
    layout->setFont(createFont(style, false));
    layout->setText(text);
    layout->setFormats(formats);
    layout->setCacheEnabled(true);

    qreal lineY = 0.0;
    layout->beginLayout();
    while (true)
    {
        QTextLine line = layout->createLine();
        if (!line.isValid())
        {
            break;
        }

        line.setLineWidth(width);
        const qreal naturalHeight = line.ascent() + line.descent();
        const qreal lineHeight = qMax(qreal(style.lineHeight), std::ceil(naturalHeight));
        line.setPosition(QPointF(0.0, lineY + (lineHeight - naturalHeight) / 2.0));
        lineY += lineHeight;
    }
    layout->endLayout();

    item->position = QPointF(x, y);
    item->bounds = QRectF(x, y, width, lineY);

    // Rectangles of the links, line by line
    for (const LinkRange& range : linkRanges)
    {
        for (int i = 0; i < layout->lineCount(); ++i)
        {
            const QTextLine line = layout->lineAt(i);
            const int lineStart = line.textStart();
            const int lineEnd = lineStart + line.textLength();
            const int start = qMax(range.start, lineStart);
            const int end = qMin(range.start + range.length, lineEnd);
            if (start >= end)
            {
                continue;
            }

            int startCursor = start;
            int endCursor = end;
            const qreal x1 = line.cursorToX(&startCursor);
            const qreal x2 = line.cursorToX(&endCursor);
            PDFAppMarkdownLayout::LinkArea area;
            area.link = range.link;
            area.text = int(m_layout->texts.size());
            area.rect = QRectF(x + qMin(x1, x2), y + line.y(), std::abs(x2 - x1), line.ascent() + line.descent());
            area.baseline = y + line.y() + line.ascent();
            m_layout->linkAreas.push_back(area);
        }
    }

    m_layout->texts.push_back(std::move(item));
    return y + lineY;
}

qreal PDFAppMarkdownLayoutBuilder::addPlainText(const QString& text, const TextStyle& style, qreal x, qreal width, qreal y, Qt::Alignment alignment, bool wrapAnywhere)
{
    // Code is wrapped anywhere (long identifiers); every chunk of a long text is laid
    // out once with the mode, so the height is the height of all chunks
    PDFAppMarkdownSpan span;
    span.text = text;
    TextStyle textStyle = style;
    textStyle.wrapAnywhere = textStyle.wrapAnywhere || wrapAnywhere;
    return addText({ span }, textStyle, x, width, y, alignment);
}

qreal PDFAppMarkdownLayoutBuilder::layoutBlocks(const std::vector<PDFAppMarkdownBlock>& blocks, qreal x, qreal width, qreal y, const Context& context)
{
    bool isFirst = true;
    for (const PDFAppMarkdownBlock& block : blocks)
    {
        if (!isFirst)
        {
            y += context.blockSpacing;
        }
        isFirst = false;
        y = layoutBlock(block, x, width, y, context);
    }
    return y;
}

qreal PDFAppMarkdownLayoutBuilder::layoutBlock(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y, const Context& context)
{
    switch (block.type)
    {
        case PDFAppMarkdownBlock::Type::Paragraph:
            return addText(block.spans, context.body, x, width, y);

        case PDFAppMarkdownBlock::Type::Heading:
        {
            m_layout->anchors.emplace_back(block.anchor, y);

            TextStyle style = context.body;
            style.italic = false;
            qreal top = 6.0;
            switch (block.level)
            {
                case 1:
                    style.pixelSize = 18;
                    style.lineHeight = 24;
                    style.weight = QFont::Bold;
                    style.color = PDFThemeColors::TextPrimary;
                    break;

                case 2:
                    style.pixelSize = 14;
                    style.lineHeight = 17;
                    style.weight = QFont::DemiBold;
                    style.color = PDFThemeColors::TextPrimary;
                    break;

                case 3:
                    style.pixelSize = 13;
                    style.lineHeight = 20;
                    style.weight = QFont::DemiBold;
                    style.color = PDFThemeColors::TextPrimary;
                    top = 4.0;
                    break;

                default:
                    style.weight = QFont::DemiBold;
                    top = 2.0;
                    break;
            }

            // Headings in nested blocks (callouts) keep the size of their text
            if (context.depth > 0)
            {
                style.pixelSize = context.body.pixelSize;
                style.lineHeight = context.body.lineHeight;
                top = 0.0;
            }
            return addText(block.spans, style, x, width, y + top);
        }

        case PDFAppMarkdownBlock::Type::List:
            return layoutList(block, x, width, y, context);

        case PDFAppMarkdownBlock::Type::ListItem:
            return layoutBlocks(block.children, x, width, y, context);

        case PDFAppMarkdownBlock::Type::CodeBlock:
        {
            // bg/subtle with the border border/default, radius 8, paddings 10 / 12
            TextStyle style;
            style.pixelSize = 12;
            style.lineHeight = 17;
            style.color = PDFThemeColors::TextPrimary;
            style.isCode = true;

            const size_t shapeIndex = m_layout->shapes.size();
            m_layout->shapes.push_back({ QRectF(), PDFStylePainter::Radii(8.0), PDFStylePainter::color(PDFThemeColors::BgSubtle), PDFStylePainter::color(PDFThemeColors::BorderDefault) });
            const QString text = block.text.isEmpty() ? QStringLiteral(" ") : block.text;
            const qreal bottom = addPlainText(text, style, x + 12.0, width - 24.0, y + 10.0, Qt::Alignment(Qt::AlignLeft), true);
            m_layout->shapes[shapeIndex].rect = QRectF(x, y, width, bottom + 10.0 - y);
            return bottom + 10.0;
        }

        case PDFAppMarkdownBlock::Type::Quote:
            return layoutQuote(block, x, width, y, context);

        case PDFAppMarkdownBlock::Type::Table:
            return layoutTable(block, x, width, y);

        case PDFAppMarkdownBlock::Type::Image:
            return layoutPicture(block, x, width, y, context);

        case PDFAppMarkdownBlock::Type::Rule:
            m_layout->shapes.push_back({ QRectF(x, y + 6.0, width, 1.0), PDFStylePainter::Radii(), PDFStylePainter::color(PDFThemeColors::BorderDivider), QColor() });
            return y + 13.0;
    }

    return y;
}

qreal PDFAppMarkdownLayoutBuilder::layoutList(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y, const Context& context)
{
    // Markers in a column of 18 px (wider for long numbers), gap 10, indent 4
    const qsizetype itemCount = qsizetype(block.children.size());
    const qint64 lastNumber = qint64(block.startNumber) + qMax<qint64>(itemCount - 1, 0);

    TextStyle markerStyle = context.body;
    markerStyle.italic = false;
    markerStyle.weight = block.ordered ? QFont::Medium : QFont::Normal;
    markerStyle.color = block.ordered ? PDFThemeColors::TextAccent : PDFThemeColors::TextTertiary;

    qreal markerWidth = MARKER_WIDTH;
    if (block.ordered)
    {
        const QFontMetricsF metrics(createFont(markerStyle, false));
        markerWidth = qMax(markerWidth, std::ceil(metrics.horizontalAdvance(QString::number(lastNumber) + QStringLiteral("."))));
    }

    const qreal contentX = x + LIST_INDENT + markerWidth + MARKER_GAP;
    const qreal contentWidth = qMax(width - (contentX - x), MINIMUM_WIDTH);

    Context itemContext = context;
    itemContext.blockSpacing = LIST_ITEM_SPACING;
    itemContext.depth = context.depth + 1;

    qint64 number = block.startNumber;
    bool isFirst = true;
    for (const PDFAppMarkdownBlock& item : block.children)
    {
        if (!isFirst)
        {
            y += LIST_ITEM_SPACING;
        }
        isFirst = false;

        // ol / ul of 955:152873: the number is aligned to the text, the bullet is centered
        // in the column; a text is laid out at least MINIMUM_WIDTH wide, so the box of the
        // marker keeps the trailing edge (the center) of the column
        const QString marker = block.ordered ? QString::number(number++) + QStringLiteral(".") : QString(QChar(0x2022));
        const qreal markerBox = qMax(markerWidth, MINIMUM_WIDTH);
        const qreal markerX = x + LIST_INDENT + (block.ordered ? markerWidth - markerBox : (markerWidth - markerBox) / 2.0);
        addPlainText(marker, markerStyle, markerX, markerBox, y, block.ordered ? Qt::Alignment(Qt::AlignTrailing) : Qt::Alignment(Qt::AlignHCenter));

        const qreal bottom = layoutBlocks(item.children, contentX, contentWidth, y, itemContext);
        y = qMax(bottom, y + qreal(context.body.lineHeight));
    }
    return y;
}

qreal PDFAppMarkdownLayoutBuilder::layoutQuote(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y, const Context& context)
{
    if (block.callout == PDFAppMarkdownCallout::None)
    {
        // Plain quote: a bar of 3 px border/strong, the text text/tertiary
        Context quoteContext = context;
        quoteContext.body.color = PDFThemeColors::TextTertiary;
        quoteContext.depth = context.depth + 1;

        const size_t shapeIndex = m_layout->shapes.size();
        m_layout->shapes.push_back({ QRectF(), PDFStylePainter::Radii(1.5), PDFStylePainter::color(PDFThemeColors::BorderStrong), QColor() });
        const qreal bottom = layoutBlocks(block.children, x + 15.0, width - 15.0, y, quoteContext);
        m_layout->shapes[shapeIndex].rect = QRectF(x, y, 3.0, qMax(bottom - y, qreal(context.body.lineHeight)));
        return qMax(bottom, y + qreal(context.body.lineHeight));
    }

    // Callout (callout-note, callout-tip of 955:152873): the hue of the kind, border 1,
    // paddings 12 / 14, the icon 16, gap 10, the title Semi Bold 12, the body 12 / 17
    PDFAccentHue hue = PDFAccentHue::Blue;
    QString iconName = QStringLiteral("info");
    switch (block.callout)
    {
        case PDFAppMarkdownCallout::Note:
            break;
        case PDFAppMarkdownCallout::Tip:
            iconName = QStringLiteral("zap");
            break;
        case PDFAppMarkdownCallout::Important:
            hue = PDFAccentHue::Violet;
            iconName = QStringLiteral("alert-circle");
            break;
        case PDFAppMarkdownCallout::Warning:
            hue = PDFAccentHue::Amber;
            iconName = QStringLiteral("alert-circle");
            break;
        case PDFAppMarkdownCallout::Caution:
            hue = PDFAccentHue::Red;
            iconName = QStringLiteral("alert-triangle");
            break;
        case PDFAppMarkdownCallout::None:
            break;
    }

    const size_t shapeIndex = m_layout->shapes.size();
    m_layout->shapes.push_back({ QRectF(), PDFStylePainter::Radii(8.0),
                                 PDFStylePainter::color(PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background)),
                                 PDFStylePainter::color(PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Border)) });
    m_layout->icons.push_back({ QRectF(x + 15.0, y + 13.0, 16.0, 16.0), iconName, PDFStylePainter::color(PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon)) });

    const qreal textX = x + 41.0;
    const qreal textWidth = width - 41.0 - 15.0;
    qreal textY = y + 13.0;

    if (!block.spans.empty())
    {
        TextStyle titleStyle;
        titleStyle.pixelSize = 12;
        titleStyle.lineHeight = 15;
        titleStyle.weight = QFont::DemiBold;
        titleStyle.color = PDFThemeColors::TextPrimary;
        textY = addText(block.spans, titleStyle, textX, textWidth, textY);
        if (!block.children.empty())
        {
            textY += 2.0;
        }
    }

    Context bodyContext;
    bodyContext.body.pixelSize = 12;
    bodyContext.body.lineHeight = 17;
    bodyContext.body.color = PDFThemeColors::TextSecondary;
    bodyContext.blockSpacing = 6.0;
    bodyContext.depth = context.depth + 1;
    textY = layoutBlocks(block.children, textX, textWidth, textY, bodyContext);

    const qreal bottom = qMax(textY, y + 13.0 + 16.0) + 13.0;
    m_layout->shapes[shapeIndex].rect = QRectF(x, y, width, bottom - y);
    return bottom;
}

qreal PDFAppMarkdownLayoutBuilder::measureNatural(const PDFAppMarkdownSpans& spans, const TextStyle& style) const
{
    qreal result = 0.0;
    for (const PDFAppMarkdownSpan& span : spans)
    {
        TextStyle spanStyle = style;
        spanStyle.isCode = span.code;
        spanStyle.italic = span.italic;
        const QFontMetricsF metrics(createFont(spanStyle, span.bold));
        result += metrics.horizontalAdvance(span.text.left(512));
    }
    return result;
}

qreal PDFAppMarkdownLayoutBuilder::layoutTable(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y)
{
    // table of 955:152873: border border/default radius 8, the header bg/subtle Semi
    // Bold 12 text/secondary, cells 12 / 17 (the first column Medium text/primary),
    // paddings 8 / 12, gaps 12 between the columns, rows divided by border/divider
    const size_t columns = block.alignments.size();
    if (columns == 0 || block.rows.empty())
    {
        return y;
    }

    TextStyle headerStyle;
    headerStyle.pixelSize = 12;
    headerStyle.lineHeight = 17;
    headerStyle.weight = QFont::DemiBold;
    headerStyle.color = PDFThemeColors::TextSecondary;

    TextStyle firstStyle = headerStyle;
    firstStyle.weight = QFont::Medium;
    firstStyle.color = PDFThemeColors::TextPrimary;

    TextStyle cellStyle = headerStyle;
    cellStyle.weight = QFont::Normal;

    const qreal gap = 12.0;
    const qreal available = qMax(width - 2.0 - 24.0 - gap * qreal(columns - 1), qreal(columns) * 24.0);

    // Natural widths of the columns, distributed proportionally to the width
    std::vector<qreal> natural(columns, 1.0);
    for (size_t row = 0; row < block.rows.size(); ++row)
    {
        for (size_t column = 0; column < columns && column < block.rows[row].size(); ++column)
        {
            const TextStyle& style = row == 0 ? headerStyle : (column == 0 ? firstStyle : cellStyle);
            natural[column] = qMax(natural[column], measureNatural(block.rows[row][column], style));
        }
    }

    qreal naturalSum = 0.0;
    for (qreal value : natural)
    {
        naturalSum += value;
    }

    const qreal minimum = qMin(available / qreal(columns), 60.0);
    std::vector<qreal> widths(columns, 0.0);
    qreal sum = 0.0;
    for (size_t column = 0; column < columns; ++column)
    {
        widths[column] = qMax(minimum, available * natural[column] / naturalSum);
        sum += widths[column];
    }
    for (qreal& value : widths)
    {
        value = value * available / sum;
    }

    const size_t tableShape = m_layout->shapes.size();
    m_layout->shapes.push_back({ QRectF(), PDFStylePainter::Radii(8.0), QColor(), PDFStylePainter::color(PDFThemeColors::BorderDefault) });

    qreal rowY = y + 1.0;
    for (size_t row = 0; row < block.rows.size(); ++row)
    {
        const bool isHeader = row == 0;
        const size_t rowShape = m_layout->shapes.size();
        if (isHeader)
        {
            m_layout->shapes.push_back({ QRectF(), PDFStylePainter::Radii(7.0, 7.0, 0.0, 0.0), PDFStylePainter::color(PDFThemeColors::BgSubtle), QColor() });
        }
        else
        {
            m_layout->shapes.push_back({ QRectF(x + 1.0, rowY, width - 2.0, 1.0), PDFStylePainter::Radii(), PDFStylePainter::color(PDFThemeColors::BorderDivider), QColor() });
            rowY += 1.0;
        }

        qreal cellX = x + 1.0 + 12.0;
        qreal rowBottom = rowY + 8.0 + 17.0;
        for (size_t column = 0; column < columns; ++column)
        {
            const TextStyle& style = isHeader ? headerStyle : (column == 0 ? firstStyle : cellStyle);
            if (column < block.rows[row].size())
            {
                const Qt::Alignment alignment = Qt::Alignment(block.alignments[column] == Qt::AlignLeft ? int(Qt::AlignLeading) :
                                                              block.alignments[column] == Qt::AlignRight ? int(Qt::AlignTrailing) : int(Qt::AlignHCenter));
                rowBottom = qMax(rowBottom, addText(block.rows[row][column], style, cellX, widths[column], rowY + 8.0, alignment));
            }
            cellX += widths[column] + gap;
        }

        rowBottom += 8.0;
        if (isHeader)
        {
            m_layout->shapes[rowShape].rect = QRectF(x + 1.0, rowY, width - 2.0, rowBottom - rowY);
        }
        rowY = rowBottom;
    }

    const qreal bottom = rowY + 1.0;
    m_layout->shapes[tableShape].rect = QRectF(x, y, width, bottom - y);
    return bottom;
}

QImage PDFAppMarkdownLayoutBuilder::loadPicture(const QString& source) const
{
    // Only pictures of the resources, never a file of the disk or of the network
    QString path = source;
    if (!path.startsWith(QStringLiteral(":/")))
    {
        if (path.contains(QStringLiteral(":")) || path.startsWith(QChar(u'/')) || path.startsWith(QChar(u'\\')))
        {
            return QImage();
        }
        path = m_resourceBase + path;
    }

    if (!path.startsWith(QStringLiteral(":/")) || path.contains(QStringLiteral("..")) || path.contains(QChar(u'\\')))
    {
        return QImage();
    }

    QImageReader reader(path);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > MAXIMUM_PICTURE_SIZE || size.height() > MAXIMUM_PICTURE_SIZE)
    {
        return QImage();
    }
    return reader.read();
}

qreal PDFAppMarkdownLayoutBuilder::layoutPicture(const PDFAppMarkdownBlock& block, qreal x, qreal width, qreal y, const Context& context)
{
    QImage image = loadPicture(block.source);
    if (image.isNull())
    {
        // A picture, which is not available, is its alternative text
        TextStyle style = context.body;
        style.italic = true;
        style.color = PDFThemeColors::TextTertiary;
        return addPlainText(block.text.isEmpty() ? block.source : block.text, style, x, width, y);
    }

    const QSizeF logicalSize = QSizeF(image.size()) / qMax(image.devicePixelRatio(), 1.0);
    const qreal scale = qMin(1.0, qMax(width, MINIMUM_WIDTH) / qMax(logicalSize.width(), 1.0));
    const QSizeF size = logicalSize * scale;
    m_layout->pictures.push_back({ QRectF(QPointF(x, y), size), std::move(image) });
    return y + size.height();
}

void PDFAppMarkdownLayoutBuilder::mirrorRect(QRectF& rect, qreal totalWidth)
{
    rect.moveLeft(totalWidth - rect.right());
}

void PDFAppMarkdownLayoutBuilder::mirror(qreal totalWidth)
{
    for (PDFAppMarkdownLayout::Shape& shape : m_layout->shapes)
    {
        mirrorRect(shape.rect, totalWidth);
        shape.radii = shape.radii.mirrored();
    }

    // A text keeps its lines (the layout aligns them by its direction), only its box
    // moves; the rectangles of its links move with it
    std::vector<qreal> offsets;
    offsets.reserve(m_layout->texts.size());
    for (std::unique_ptr<PDFAppMarkdownLayout::Text>& text : m_layout->texts)
    {
        const qreal oldLeft = text->bounds.left();
        mirrorRect(text->bounds, totalWidth);
        text->position.setX(text->bounds.left());
        offsets.push_back(text->bounds.left() - oldLeft);
    }
    for (PDFAppMarkdownLayout::LinkArea& area : m_layout->linkAreas)
    {
        if (area.text >= 0 && area.text < int(offsets.size()))
        {
            area.rect.translate(offsets[size_t(area.text)], 0.0);
        }
    }

    for (PDFAppMarkdownLayout::Icon& icon : m_layout->icons)
    {
        mirrorRect(icon.rect, totalWidth);
    }
    for (PDFAppMarkdownLayout::Picture& picture : m_layout->pictures)
    {
        mirrorRect(picture.rect, totalWidth);
    }
}

PDFAppMarkdownView::PDFAppMarkdownView(QWidget* parent) :
    BaseClass(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);

    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setFocusPolicy(Qt::NoFocus);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppMarkdownView::onThemeChanged);
}

PDFAppMarkdownView::~PDFAppMarkdownView() = default;

void PDFAppMarkdownView::setMarkdown(const QString& text)
{
    setDocument(PDFAppMarkdownParser::parse(text));
}

void PDFAppMarkdownView::setDocument(const PDFAppMarkdownDocument& document)
{
    m_document = document;
    m_hoveredLink = -1;
    m_pressedLink = -1;
    m_focusedLink = -1;
    unsetCursor();
    invalidateLayout();
    updateFocusPolicy();
}

void PDFAppMarkdownView::setResourceBase(const QString& base)
{
    m_resourceBase = base;
    invalidateLayout();
}

void PDFAppMarkdownView::setCardVisible(bool isVisible)
{
    if (m_isCardVisible != isVisible)
    {
        m_isCardVisible = isVisible;
        invalidateLayout();
    }
}

void PDFAppMarkdownView::invalidateLayout()
{
    m_layouts.clear();
    updateGeometry();
    update();
}

void PDFAppMarkdownView::onThemeChanged()
{
    // Colors of the formats are taken at the layout
    m_layouts.clear();
    update();
}

void PDFAppMarkdownView::updateFocusPolicy()
{
    const bool hasLinks = !m_document.getLinks().isEmpty();
    setFocusPolicy(hasLinks ? Qt::TabFocus : Qt::NoFocus);
}

const PDFAppMarkdownLayout& PDFAppMarkdownView::getLayout(int width) const
{
    width = qMax(width, 1);
    for (const std::unique_ptr<PDFAppMarkdownLayout>& layout : m_layouts)
    {
        if (layout->width == width)
        {
            return *layout;
        }
    }

    std::unique_ptr<PDFAppMarkdownLayout> layout = std::make_unique<PDFAppMarkdownLayout>();
    layout->width = width;

    PDFAppMarkdownLayoutBuilder builder(layout.get(), m_resourceBase, layoutDirection());
    PDFAppMarkdownLayoutBuilder::Context context;

    const qreal paddingSide = m_isCardVisible ? PADDING_SIDE : 0.0;
    const qreal paddingTop = m_isCardVisible ? PADDING_TOP : 0.0;
    const qreal paddingBottom = m_isCardVisible ? PADDING_BOTTOM : 0.0;
    const qreal bottom = builder.layoutBlocks(m_document.blocks, paddingSide, qreal(width) - 2.0 * paddingSide, paddingTop, context);
    layout->height = std::ceil(bottom + paddingBottom);

    if (layoutDirection() == Qt::RightToLeft)
    {
        builder.mirror(qreal(width));
    }

    // Two widths are kept: the width of the view and the width asked by heightForWidth
    if (m_layouts.size() >= 2)
    {
        m_layouts.erase(m_layouts.begin());
    }
    m_layouts.push_back(std::move(layout));
    return *m_layouts.back();
}

bool PDFAppMarkdownView::hasHeightForWidth() const
{
    return true;
}

int PDFAppMarkdownView::heightForWidth(int width) const
{
    return qMax(int(getLayout(width).height), 1);
}

QSize PDFAppMarkdownView::sizeHint() const
{
    const int width = 640;
    return QSize(width, heightForWidth(width));
}

QSize PDFAppMarkdownView::minimumSizeHint() const
{
    return QSize(160, 0);
}

int PDFAppMarkdownView::getAnchorPosition(const QString& anchor) const
{
    const PDFAppMarkdownLayout& layout = getLayout(width());
    for (const auto& [name, position] : layout.anchors)
    {
        if (name == anchor)
        {
            return int(position);
        }
    }
    return -1;
}

int PDFAppMarkdownView::getLinkCount() const
{
    return int(getLayout(width()).linkTargets.size());
}

QString PDFAppMarkdownView::getLinkTarget(int index) const
{
    const PDFAppMarkdownLayout& layout = getLayout(width());
    return index >= 0 && index < int(layout.linkTargets.size()) ? layout.linkTargets[size_t(index)] : QString();
}

PDFAppMarkdownLinkKind PDFAppMarkdownView::getLinkKind(int index) const
{
    const PDFAppMarkdownLayout& layout = getLayout(width());
    return index >= 0 && index < int(layout.linkKinds.size()) ? layout.linkKinds[size_t(index)] : PDFAppMarkdownLinkKind::None;
}

QList<QRect> PDFAppMarkdownView::getLinkRects(int index) const
{
    QList<QRect> rects;
    for (const PDFAppMarkdownLayout::LinkArea& area : getLayout(width()).linkAreas)
    {
        if (area.link == index)
        {
            rects << area.rect.toAlignedRect();
        }
    }
    return rects;
}

QString PDFAppMarkdownView::getPlainText() const
{
    return m_document.toPlainText();
}

int PDFAppMarkdownView::getLinkAt(const QPointF& point) const
{
    for (const PDFAppMarkdownLayout::LinkArea& area : getLayout(width()).linkAreas)
    {
        if (area.rect.adjusted(0.0, -1.0, 0.0, 1.0).contains(point))
        {
            return area.link;
        }
    }
    return -1;
}

void PDFAppMarkdownView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const PDFAppMarkdownLayout& layout = getLayout(width());
    const QRectF clip = event->rect();

    if (m_isCardVisible)
    {
        const QRectF card(0.0, 0.0, width(), height());
        PDFStylePainter::fill(&painter, card, PDFStylePainter::Radii(12.0), PDFThemeColors::BgSurface);
        PDFStylePainter::strokeInside(&painter, card, PDFStylePainter::Radii(12.0), PDFThemeColors::BorderDefault);
    }

    for (const PDFAppMarkdownLayout::Shape& shape : layout.shapes)
    {
        if (!shape.rect.intersects(clip))
        {
            continue;
        }

        if (shape.fill.isValid())
        {
            if (shape.rect.height() <= 1.0 || shape.rect.width() <= 1.0)
            {
                PDFStylePainter::fillRect(&painter, shape.rect, shape.fill);
            }
            else
            {
                PDFStylePainter::fill(&painter, shape.rect, shape.radii, shape.fill);
            }
        }
        if (shape.border.isValid())
        {
            PDFStylePainter::strokeInside(&painter, shape.rect, shape.radii, shape.border);
        }
    }

    for (const PDFAppMarkdownLayout::Picture& picture : layout.pictures)
    {
        if (picture.rect.intersects(clip))
        {
            painter.drawImage(picture.rect, picture.image);
        }
    }

    for (const PDFAppMarkdownLayout::Icon& icon : layout.icons)
    {
        if (icon.rect.intersects(clip))
        {
            PDFStylePainter::icon(&painter, icon.rect, icon.name, icon.color);
        }
    }

    // The formats have their colors: the pen is a token too (never the palette)
    painter.setPen(PDFStylePainter::color(PDFThemeColors::TextSecondary));
    for (const std::unique_ptr<PDFAppMarkdownLayout::Text>& text : layout.texts)
    {
        if (text->bounds.intersects(clip))
        {
            text->layout->draw(&painter, text->position, QList<QTextLayout::FormatRange>(), clip);
        }
    }

    // The hovered link is underlined, the focused link has the focus ring
    for (const PDFAppMarkdownLayout::LinkArea& area : layout.linkAreas)
    {
        if (area.link == m_hoveredLink || area.link == m_pressedLink)
        {
            const PDFThemeColors::Color color = area.link == m_pressedLink ? PDFThemeColors::TextAccentPressed : PDFThemeColors::TextAccent;
            PDFStylePainter::fillRect(&painter, QRectF(area.rect.left(), std::round(area.baseline) + 1.0, area.rect.width(), 1.0), color);
        }

        if (area.link == m_focusedLink && PDFWidgetStyle::isFocusRingVisible(this))
        {
            PDFStylePainter::focusRing(&painter, area.rect.adjusted(-2.0, -1.0, 2.0, 1.0), PDFStylePainter::Radii(3.0), false);
        }
    }
}

void PDFAppMarkdownView::setHoveredLink(int link)
{
    if (m_hoveredLink == link)
    {
        return;
    }

    m_hoveredLink = link;
    if (link >= 0)
    {
        setCursor(Qt::PointingHandCursor);
    }
    else
    {
        unsetCursor();
    }
    update();
}

void PDFAppMarkdownView::setFocusedLink(int link)
{
    if (m_focusedLink == link)
    {
        return;
    }

    m_focusedLink = link;
    update();
    ensureLinkVisible(link);
}

void PDFAppMarkdownView::ensureLinkVisible(int link)
{
    const QList<QRect> rects = getLinkRects(link);
    if (rects.isEmpty())
    {
        return;
    }

    // The scroll area of the page scrolls the link into its view
    for (QWidget* widget = parentWidget(); widget; widget = widget->parentWidget())
    {
        QScrollArea* scrollArea = qobject_cast<QScrollArea*>(widget);
        if (scrollArea && scrollArea->widget() && scrollArea->widget()->isAncestorOf(this))
        {
            const QPoint center = mapTo(scrollArea->widget(), rects.first().center());
            scrollArea->ensureVisible(center.x(), center.y(), 50, 50);
            break;
        }
    }
}

void PDFAppMarkdownView::activateLink(int link)
{
    const PDFAppMarkdownLayout& layout = getLayout(width());
    if (link < 0 || link >= int(layout.linkTargets.size()))
    {
        return;
    }

    const QString target = layout.linkTargets[size_t(link)];
    const PDFAppMarkdownLinkKind kind = layout.linkKinds[size_t(link)];
    Q_EMIT linkActivated(target, kind);
}

void PDFAppMarkdownView::mouseMoveEvent(QMouseEvent* event)
{
    setHoveredLink(getLinkAt(event->position()));
    BaseClass::mouseMoveEvent(event);
}

void PDFAppMarkdownView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_pressedLink = getLinkAt(event->position());
        if (m_pressedLink >= 0)
        {
            update();
            event->accept();
            return;
        }
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppMarkdownView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressedLink >= 0)
    {
        const int pressedLink = m_pressedLink;
        m_pressedLink = -1;
        update();
        event->accept();

        if (getLinkAt(event->position()) == pressedLink)
        {
            // The slot may delete the view
            activateLink(pressedLink);
        }
        return;
    }
    BaseClass::mouseReleaseEvent(event);
}

void PDFAppMarkdownView::leaveEvent(QEvent* event)
{
    setHoveredLink(-1);
    BaseClass::leaveEvent(event);
}

void PDFAppMarkdownView::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    // Enter of the keypad comes with Qt::KeypadModifier
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) && modifiers == Qt::NoModifier && m_focusedLink >= 0)
    {
        event->accept();
        activateLink(m_focusedLink);
        return;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppMarkdownView::focusInEvent(QFocusEvent* event)
{
    // The keyboard (Tab, Backtab) enters the view at its first or last link
    const int count = getLinkCount();
    const bool isKeyboard = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    if (count > 0 && m_focusedLink < 0 && isKeyboard)
    {
        setFocusedLink(event->reason() == Qt::BacktabFocusReason ? count - 1 : 0);
    }
    BaseClass::focusInEvent(event);
    update();
}

void PDFAppMarkdownView::focusOutEvent(QFocusEvent* event)
{
    // The next Tab into the view starts at its first (Backtab at its last) link; a
    // switch to another window or a popup keeps the link (the focus comes back to it)
    if (event->reason() != Qt::ActiveWindowFocusReason && event->reason() != Qt::PopupFocusReason)
    {
        m_focusedLink = -1;
    }
    BaseClass::focusOutEvent(event);
    update();
}

bool PDFAppMarkdownView::focusNextPrevChild(bool next)
{
    if (hasFocus() && m_focusedLink >= 0)
    {
        const int link = m_focusedLink + (next ? 1 : -1);
        if (link >= 0 && link < getLinkCount())
        {
            setFocusedLink(link);
            return true;
        }
    }
    return BaseClass::focusNextPrevChild(next);
}

void PDFAppMarkdownView::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::FontChange:
        case QEvent::LayoutDirectionChange:
        case QEvent::StyleChange:
        case QEvent::LanguageChange:
            invalidateLayout();
            break;

        default:
            break;
    }
}

void PDFAppMarkdownView::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);

    // The links moved: the pointer is over none of them (also the cursor is reset)
    setHoveredLink(-1);
    update();
}

}   // namespace pdf
