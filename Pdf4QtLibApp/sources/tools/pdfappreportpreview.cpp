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

#include "pdfappreportpreview.h"
#include "pdfapptoolvalueformatter.h"

#include "pdfdeferreddeletion.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>

#include <algorithm>
#include <limits>

namespace pdf
{

/// The fonts and the measures of the preview of Document Report
class PDFAppReportPreviewHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppReportPreview)

public:
    PDFAppReportPreviewHelper() = delete;

    static constexpr int CELL_PADDING = 12;
    static constexpr int GAP = 12;
    static constexpr int CODE_PADDING = 12;
    static constexpr int INDENT = 16;
    static constexpr int MINIMUM_COLUMN = 40;
    static constexpr int MEASURED_ROWS = 50;    ///< The rows measured for the widths of the columns

    static QFont titleFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::DialogTitle); }
    static QFont headingFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold); }
    static QFont paragraphFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular); }
    static QFont cellFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::Small); }
    static QFont headFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption); }
    static QFont codeFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::Small); }

    /// A numeric column is right-aligned
    static bool isNumeric(PDFDocumentToolTableValueType type)
    {
        return type == PDFDocumentToolTableValueType::Integer || type == PDFDocumentToolTableValueType::Real;
    }

    /// The height of a wrapped text
    static int textHeight(const QString& text, const QFont& font, int width)
    {
        return QFontMetrics(font).boundingRect(QRect(0, 0, qMax(1, width), 1 << 24), Qt::TextWordWrap, text).height();
    }
};

PDFAppReportPreview::PDFAppReportPreview(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("reportPreview"));
    setAccessibleName(tr("Preview of the report"));
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppReportPreview::onThemeChanged);
}

PDFAppReportPreview::~PDFAppReportPreview() = default;

void PDFAppReportPreview::setReport(const PDFDocumentToolTableSection& report, PDFDocumentToolDateFormat dateFormat)
{
    // The blocks point into the model: they are laid out again from the copy
    m_blocks.clear();
    m_report = report;
    m_dateFormat = dateFormat;
    m_layoutWidth = -1;
    layoutBlocks(width());
    updateGeometry();
    update();
}

void PDFAppReportPreview::setDimmed(bool isDimmed)
{
    if (m_isDimmed != isDimmed)
    {
        m_isDimmed = isDimmed;
        update();
    }
}

int PDFAppReportPreview::getSectionTop(const QString& sectionName) const
{
    for (const Block& block : m_blocks)
    {
        if (block.kind == Block::Kind::Heading && block.sectionName == sectionName)
        {
            return block.top;
        }
    }
    return -1;
}

QStringList PDFAppReportPreview::getBlockTexts() const
{
    QStringList texts;
    for (const Block& block : m_blocks)
    {
        if (block.kind == Block::Kind::Table && block.table)
        {
            QStringList cells;
            for (const PDFDocumentToolTableColumn& column : block.table->columns)
            {
                cells << column.title;
            }
            for (int row = 0; row < block.shownRows; ++row)
            {
                const std::vector<QVariant>& values = block.table->rows[row];
                for (size_t i = 0; i < block.table->columns.size(); ++i)
                {
                    cells << (i < values.size() ? getCellText(values[i], block.table->columns[i]) : QString());
                }
            }
            texts << cells.join(QLatin1Char('|'));
        }
        else
        {
            texts << block.text;
        }
    }
    return texts;
}

QString PDFAppReportPreview::getCellText(const QVariant& value, const PDFDocumentToolTableColumn& column) const
{
    if (!value.isValid() || value.isNull())
    {
        return QString();
    }

    static const PDFAppToolValueFormatter formatter;
    switch (column.type)
    {
        case PDFDocumentToolTableValueType::Integer:
            return formatter.formatInteger(value.toLongLong());
        case PDFDocumentToolTableValueType::Real:
            return formatter.formatReal(value.toDouble(), column.decimals);
        case PDFDocumentToolTableValueType::DateTime:
            return formatter.formatDateTime(value.toDateTime(), m_dateFormat);
        case PDFDocumentToolTableValueType::Text:
        case PDFDocumentToolTableValueType::DocumentText:
            break;
    }
    return value.toString();
}

QSize PDFAppReportPreview::sizeHint() const
{
    return QSize(774, qMax(200, m_contentHeight));
}

QSize PDFAppReportPreview::minimumSizeHint() const
{
    return QSize(240, 100);
}

int PDFAppReportPreview::heightForWidth(int width) const
{
    if (width == m_layoutWidth)
    {
        return m_contentHeight;
    }
    PDFAppReportPreview* self = const_cast<PDFAppReportPreview*>(this);
    self->layoutBlocks(width);
    return m_contentHeight;
}

void PDFAppReportPreview::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    if (width() != m_layoutWidth)
    {
        layoutBlocks(width());
        updateGeometry();
    }
}

void PDFAppReportPreview::onThemeChanged()
{
    update();
}

void PDFAppReportPreview::layoutBlocks(int width)
{
    using Helper = PDFAppReportPreviewHelper;

    m_blocks.clear();
    m_layoutWidth = width;
    const int contentWidth = qMax(100, width - 2 * PADDING);
    int y = PADDING;

    // The title of the report (484:15472 "Report on Project_Report.pdf")
    if (!m_report.title.isEmpty())
    {
        Block block;
        block.kind = Block::Kind::Title;
        block.top = y;
        block.text = m_report.title;
        block.height = Helper::textHeight(block.text, Helper::titleFont(), contentWidth);
        y += block.height + Helper::GAP;
        m_blocks.push_back(std::move(block));
    }

    addSectionBlocks(m_report, 0, contentWidth, &y);
    m_contentHeight = y + PADDING - Helper::GAP;
}

void PDFAppReportPreview::addSectionBlocks(const PDFDocumentToolTableSection& section, int depth, int width, int* y)
{
    using Helper = PDFAppReportPreviewHelper;

    if (depth > MAXIMUM_DEPTH)
    {
        return;
    }

    // The nested sections are indented from the third level (the structure tree, 484:17763)
    const int indent = qMax(0, depth - 1) * Helper::INDENT;
    const int blockWidth = qMax(60, width - indent);
    for (const PDFDocumentToolTableItem& item : section.items)
    {
        Block block;
        block.top = *y;
        block.indent = indent;
        switch (item.kind)
        {
            case PDFDocumentToolTableItem::Kind::Paragraph:
                block.kind = Block::Kind::Paragraph;
                block.text = item.text;
                block.height = Helper::textHeight(block.text, Helper::paragraphFont(), blockWidth);
                break;

            case PDFDocumentToolTableItem::Kind::Code:
            {
                // At most MAXIMUM_CODE_LINES lines, each on one line (a script keeps its lines, never run)
                block.kind = Block::Kind::Code;
                QStringList lines = item.text.split(QLatin1Char('\n'));
                if (lines.size() > MAXIMUM_CODE_LINES)
                {
                    const qsizetype more = lines.size() - MAXIMUM_CODE_LINES;
                    lines = lines.mid(0, MAXIMUM_CODE_LINES);
                    lines << tr("… %Ln more lines follow in the saved report.", nullptr, int(qMin<qsizetype>(more, std::numeric_limits<int>::max())));
                }
                block.text = lines.join(QLatin1Char('\n'));
                block.height = int(lines.size()) * QFontMetrics(Helper::codeFont()).lineSpacing() + 2 * Helper::CODE_PADDING;
                break;
            }

            case PDFDocumentToolTableItem::Kind::Table:
            {
                block.kind = Block::Kind::Table;
                block.table = &item.table;
                block.shownRows = int(qMin<size_t>(item.table.rows.size(), size_t(MAXIMUM_TABLE_ROWS)));
                block.height = (1 + block.shownRows) * ROW_HEIGHT + 2;

                // The widths of the columns by their content (the first rows), shrunk to the width of the card
                const int columnCount = int(item.table.columns.size());
                std::vector<int> widths(columnCount, Helper::MINIMUM_COLUMN);
                const QFontMetrics cellMetrics(Helper::cellFont());
                const QFontMetrics headMetrics(Helper::headFont());
                for (int column = 0; column < columnCount; ++column)
                {
                    widths[column] = qMax(widths[column], headMetrics.horizontalAdvance(item.table.columns[column].title) + 2 * Helper::CELL_PADDING);
                    for (int row = 0; row < qMin(block.shownRows, int(Helper::MEASURED_ROWS)); ++row)
                    {
                        const std::vector<QVariant>& cells = item.table.rows[row];
                        if (column < int(cells.size()))
                        {
                            widths[column] = qMax(widths[column], qMin(400, cellMetrics.horizontalAdvance(getCellText(cells[column], item.table.columns[column]))) + 2 * Helper::CELL_PADDING);
                        }
                    }
                }
                int total = 0;
                for (const int value : widths)
                {
                    total += value;
                }
                const int available = blockWidth - 2;
                if (total > 0 && total != available)
                {
                    // The last text column takes the rest (484:15472 Value, 484:17261 Name), so the numbers stay
                    // beside their heads; a table of numbers only gives it to its last column. Or all shrink in
                    // proportion.
                    if (total < available)
                    {
                        int restColumn = columnCount - 1;
                        for (int column = columnCount - 1; column >= 0; --column)
                        {
                            if (!Helper::isNumeric(item.table.columns[column].type))
                            {
                                restColumn = column;
                                break;
                            }
                        }
                        widths[restColumn] += available - total;
                    }
                    else
                    {
                        for (int& value : widths)
                        {
                            value = qMax(Helper::MINIMUM_COLUMN / 2, value * available / total);
                        }
                    }
                }
                block.columnWidths = std::move(widths);

                if (int(item.table.rows.size()) > block.shownRows)
                {
                    m_blocks.push_back(block);
                    *y += block.height + Helper::GAP;

                    Block note;
                    note.kind = Block::Kind::Paragraph;
                    note.top = *y;
                    note.indent = indent;
                    note.text = tr("%Ln more rows follow in the saved report.", nullptr, int(qMin<size_t>(item.table.rows.size() - size_t(block.shownRows), size_t(std::numeric_limits<int>::max()))));
                    note.height = Helper::textHeight(note.text, Helper::paragraphFont(), blockWidth);
                    *y += note.height + Helper::GAP;
                    m_blocks.push_back(std::move(note));
                    continue;
                }
                break;
            }

            case PDFDocumentToolTableItem::Kind::Section:
            {
                if (item.sectionIndex < 0 || item.sectionIndex >= int(section.sections.size()))
                {
                    continue;
                }

                const PDFDocumentToolTableSection& nested = section.sections[item.sectionIndex];
                if (!nested.title.isEmpty())
                {
                    Block heading;
                    heading.kind = Block::Kind::Heading;
                    heading.top = *y;
                    heading.indent = indent;
                    heading.text = nested.title;
                    heading.sectionName = nested.name;
                    heading.height = Helper::textHeight(heading.text, Helper::headingFont(), blockWidth);
                    *y += heading.height + Helper::GAP;
                    m_blocks.push_back(std::move(heading));
                }
                addSectionBlocks(nested, depth + 1, width, y);
                continue;
            }
        }

        *y += block.height + Helper::GAP;
        m_blocks.push_back(std::move(block));
    }
}

void PDFAppReportPreview::paintEvent(QPaintEvent* event)
{
    // A layout may have measured another width (heightForWidth): the blocks of this width
    if (m_layoutWidth != width())
    {
        layoutBlocks(width());
    }

    QPainter painter(this);
    PDFPainterStateGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);

    // The white card of the canvas (484:15472: bg/surface, radius 12)
    PDFStylePainter::fill(&painter, QRectF(rect()), PDFStylePainter::Radii(12), PDFThemeColors::BgSurface);
    if (m_isDimmed)
    {
        painter.setOpacity(0.5);
    }

    // Only the blocks in the rectangle of the event (a report of many rows)
    const QRect area = event->rect();
    for (const Block& block : m_blocks)
    {
        if (block.top > area.bottom() || block.top + block.height < area.top())
        {
            continue;
        }
        paintBlock(&painter, block);
    }
}

void PDFAppReportPreview::paintBlock(QPainter* painter, const Block& block) const
{
    using Helper = PDFAppReportPreviewHelper;

    const Qt::LayoutDirection direction = layoutDirection();
    const QRect logical(PADDING + block.indent, block.top, qMax(1, width() - 2 * PADDING - block.indent), block.height);
    const QRect rect = QStyle::visualRect(direction, this->rect(), logical);
    const int alignment = QStyle::visualAlignment(direction, Qt::AlignLeft | Qt::AlignTop) | Qt::TextWordWrap;
    switch (block.kind)
    {
        case Block::Kind::Title:
            painter->setFont(Helper::titleFont());
            painter->setPen(PDFStylePainter::color(PDFThemeColors::TextPrimary));
            painter->drawText(rect, alignment, block.text);
            break;

        case Block::Kind::Heading:
            painter->setFont(Helper::headingFont());
            painter->setPen(PDFStylePainter::color(PDFThemeColors::TextPrimary));
            painter->drawText(rect, alignment, block.text);
            break;

        case Block::Kind::Paragraph:
            painter->setFont(Helper::paragraphFont());
            painter->setPen(PDFStylePainter::color(PDFThemeColors::TextTertiary));
            painter->drawText(rect, alignment, block.text);
            break;

        case Block::Kind::Code:
        {
            // 484:16862: bg/subtle, radius 8, padding 12, the lines as they are
            PDFStylePainter::fill(painter, QRectF(rect), PDFStylePainter::Radii(8), PDFThemeColors::BgSubtle);
            PDFPainterStateGuard guard(painter);
            painter->setClipRect(rect.adjusted(Helper::CODE_PADDING, Helper::CODE_PADDING, -Helper::CODE_PADDING, -Helper::CODE_PADDING));
            painter->setFont(Helper::codeFont());
            painter->setPen(PDFStylePainter::color(PDFThemeColors::TextPrimary));
            const int lineHeight = QFontMetrics(Helper::codeFont()).lineSpacing();
            const QStringList lines = block.text.split(QLatin1Char('\n'));
            const QRect clip = painter->clipBoundingRect().toAlignedRect();
            for (int i = 0; i < lines.size(); ++i)
            {
                const QRect line(rect.left() + Helper::CODE_PADDING, rect.top() + Helper::CODE_PADDING + i * lineHeight, rect.width() - 2 * Helper::CODE_PADDING, lineHeight);
                if (line.bottom() < clip.top() || line.top() > clip.bottom())
                {
                    continue;
                }
                painter->drawText(line, QStyle::visualAlignment(direction, Qt::AlignLeft | Qt::AlignVCenter) | Qt::TextSingleLine, lines[i].left(4096));
            }
            break;
        }

        case Block::Kind::Table:
            paintTable(painter, block);
            break;
    }
}

void PDFAppReportPreview::paintTable(QPainter* painter, const Block& block) const
{
    using Helper = PDFAppReportPreviewHelper;

    if (!block.table)
    {
        return;
    }

    // 484:15472: a card (border/default, radius 8), the head bg/subtle with Medium 11 text/tertiary, the rows
    // Regular 12 with dividers; a number to the right
    const Qt::LayoutDirection direction = layoutDirection();
    const QRect area = rect();
    const QRect logical(PADDING + block.indent, block.top, qMax(1, width() - 2 * PADDING - block.indent), block.height);
    const QRectF card(QStyle::visualRect(direction, area, logical));
    PDFPainterStateGuard guard(painter);
    painter->setClipPath(PDFStylePainter::roundedPath(card, PDFStylePainter::Radii(8)));
    PDFStylePainter::fillRect(painter, QRectF(card.left(), card.top(), card.width(), ROW_HEIGHT + 1), PDFThemeColors::BgSubtle);

    const QFont headFont = Helper::headFont();
    const QFont cellFont = Helper::cellFont();
    const QColor headColor = PDFStylePainter::color(PDFThemeColors::TextTertiary);
    const QColor cellColor = PDFStylePainter::color(PDFThemeColors::TextPrimary);
    const std::vector<PDFDocumentToolTableColumn>& columns = block.table->columns;
    for (int row = -1; row < block.shownRows; ++row)
    {
        const int top = block.top + 1 + (row + 1) * ROW_HEIGHT;
        if (row >= 0)
        {
            const QRect divider = QStyle::visualRect(direction, area, QRect(logical.left(), top, logical.width(), 1));
            PDFStylePainter::fillRect(painter, QRectF(divider), PDFThemeColors::BorderDivider);
        }

        int x = logical.left() + 1;
        for (size_t column = 0; column < columns.size() && column < block.columnWidths.size(); ++column)
        {
            const int cellWidth = block.columnWidths[column];
            const QRect cell = QStyle::visualRect(direction, area, QRect(x + Helper::CELL_PADDING, top, qMax(1, cellWidth - 2 * Helper::CELL_PADDING), ROW_HEIGHT));
            // 484:17261: the head of a numeric column is right-aligned above its values ("Left", "Top", …)
            const bool isNumber = Helper::isNumeric(columns[column].type);
            const int alignment = QStyle::visualAlignment(direction, (isNumber ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter) | Qt::TextSingleLine;
            if (row < 0)
            {
                PDFStylePainter::text(painter, QRectF(cell), alignment, columns[column].title, headFont, headColor);
            }
            else
            {
                const std::vector<QVariant>& values = block.table->rows[row];
                const QString text = column < values.size() ? getCellText(values[column], columns[column]) : QString();
                const QString shown = QFontMetrics(cellFont).elidedText(text.left(4096), Qt::ElideRight, cell.width());
                PDFStylePainter::text(painter, QRectF(cell), alignment, shown, cellFont, cellColor);
            }
            x += cellWidth;
        }
    }

    painter->setClipping(false);
    PDFStylePainter::strokeInside(painter, card, PDFStylePainter::Radii(8), PDFThemeColors::BorderDefault);
}

}   // namespace pdf

#include "moc_pdfappreportpreview.cpp"
