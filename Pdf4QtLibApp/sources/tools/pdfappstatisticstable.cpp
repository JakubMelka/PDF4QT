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

#include "pdfappstatisticstable.h"
#include "pdfapplocale.h"
#include "pdfdocumenttool_statistics.h"

#include "pdfdeferreddeletion.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QFontMetrics>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace pdf
{

/// The texts and the geometry of the canvas of Document Statistics
class PDFAppStatisticsTableHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppStatisticsTable)

public:
    PDFAppStatisticsTableHelper() = delete;

    /// The percent text of the table ("61.42")
    static QString formatShare(double share)
    {
        return PDFAppLocale::getUiLocale().toString(share, 'f', 2);
    }

    static QFont font(PDFThemeFonts::Role role) { return PDFThemeFonts::getFont(role); }

    /// The order of the rows: the largest share first
    static bool isLargerShare(const PDFAppStatisticsRow& left, const PDFAppStatisticsRow& right) { return left.share > right.share; }
};

// ---------------------------------------------------------------- PDFAppToolCanvasMessage

PDFAppToolCanvasMessage::PDFAppToolCanvasMessage(QWidget* parent) :
    BaseClass(parent)
{
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppToolCanvasMessage::onThemeChanged);
}

PDFAppToolCanvasMessage::~PDFAppToolCanvasMessage() = default;

void PDFAppToolCanvasMessage::setMessage(const QString& iconName, const QString& title, const QString& text)
{
    m_iconName = iconName;
    m_title = title;
    m_text = text;
    setAccessibleName(title);
    setAccessibleDescription(text);
    update();
}

void PDFAppToolCanvasMessage::onThemeChanged()
{
    update();
}

void PDFAppToolCanvasMessage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas 480:15218: bg/track; the icon 24 (icon/muted), the title Semi Bold 16 and the text
    // Regular 12 in the middle, 8 apart (484:20937)
    QPainter painter(this);
    PDFPainterStateGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BgTrack);
    if (m_title.isEmpty())
    {
        return;
    }

    const QFont titleFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::DialogTitle);
    const QFont textFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::Small);
    const int iconSize = 24;
    const int titleHeight = QFontMetrics(titleFont).height();
    const int textHeight = m_text.isEmpty() ? 0 : QFontMetrics(textFont).height();
    const int totalHeight = iconSize + 8 + titleHeight + (textHeight > 0 ? 6 + textHeight : 0);
    int y = (height() - totalHeight) / 2;

    if (!m_iconName.isEmpty())
    {
        PDFStylePainter::icon(&painter, QRectF((width() - iconSize) / 2.0, y, iconSize, iconSize), m_iconName, PDFThemeColors::IconMuted);
    }
    y += iconSize + 8;
    PDFStylePainter::text(&painter, QRectF(16, y, width() - 32, titleHeight), Qt::AlignHCenter | Qt::AlignVCenter, m_title, titleFont, PDFStylePainter::color(PDFThemeColors::TextPrimary));
    y += titleHeight + 6;
    if (textHeight > 0)
    {
        PDFStylePainter::text(&painter, QRectF(16, y, width() - 32, textHeight), Qt::AlignHCenter | Qt::AlignVCenter, m_text, textFont, PDFStylePainter::color(PDFThemeColors::TextTertiary));
    }
}

// ---------------------------------------------------------------- PDFAppStatisticsTable

PDFAppStatisticsTable::PDFAppStatisticsTable(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("statisticsTable"));
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Statistics"));
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFAppStatisticsTable::onThemeChanged);
}

PDFAppStatisticsTable::~PDFAppStatisticsTable() = default;

void PDFAppStatisticsTable::setData(const QJsonObject& statistics, const QString& view)
{
    const PDFAppStatisticsRow* previous = getSelectedRowData();
    const QString previousId = previous ? previous->id : QString();
    const bool wasTypeView = isTypeView();

    m_data = statistics;
    m_view = view;
    m_rows = createRows(m_data, m_view);

    // The chosen row stays by its id in the same view; a new view starts at its first row (480:14555)
    int selected = m_rows.empty() ? -1 : 0;
    if (wasTypeView == isTypeView() && !previousId.isEmpty())
    {
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            if (m_rows[i].id == previousId)
            {
                selected = int(i);
                break;
            }
        }
    }

    updateGeometry();
    update();
    updateAccessibleText();
    setSelectedRow(selected);
}

void PDFAppStatisticsTable::clear()
{
    m_data = QJsonObject();
    m_rows.clear();
    updateGeometry();
    update();
    updateAccessibleText();
    setSelectedRow(-1);
}

bool PDFAppStatisticsTable::isTypeView() const
{
    return m_view == QLatin1String(PDFDocumentToolStatistics::VIEW_TYPE);
}

void PDFAppStatisticsTable::setSelectedRow(int row)
{
    if (row < -1 || row >= int(m_rows.size()))
    {
        row = -1;
    }
    if (m_selectedRow == row)
    {
        return;
    }

    m_selectedRow = row;
    updateAccessibleText();
    update();
    Q_EMIT selectedRowChanged(row);
}

QString PDFAppStatisticsTable::getAccessibleRowText(const PDFAppStatisticsRow& row) const
{
    using Helper = PDFAppStatisticsTableHelper;

    // The cells of the row in the order of the heads (Share (%), Count, Size (bytes))
    if (isTypeView())
    {
        return tr("%1: share %2 %, count %3", "Accessible text of a row of the statistics by type: the type, the share, the count")
               .arg(row.title, Helper::formatShare(row.share), PDFAppLocale::formatNumber(row.count));
    }
    return tr("%1: share %2 %, count %3, size %4 bytes", "Accessible text of a row of the statistics by function: the class, the share, the count, the size")
           .arg(row.title, Helper::formatShare(row.share), PDFAppLocale::formatNumber(row.count), PDFAppLocale::formatNumber(row.bytes));
}

QString PDFAppStatisticsTable::getAccessibleText() const
{
    if (m_rows.empty())
    {
        return QString();
    }

    QStringList parts;
    parts << getTitle() << getSummary();
    if (const PDFAppStatisticsRow* selected = getSelectedRowData())
    {
        parts << tr("Chosen: %1", "Accessible text of the chosen row of the statistics").arg(selected->title);
    }
    for (const PDFAppStatisticsRow& row : m_rows)
    {
        parts << getAccessibleRowText(row);
    }
    return parts.join(QStringLiteral("; "));
}

void PDFAppStatisticsTable::updateAccessibleText()
{
    setAccessibleDescription(getAccessibleText());
}

const PDFAppStatisticsRow* PDFAppStatisticsTable::getSelectedRowData() const
{
    return (m_selectedRow >= 0 && m_selectedRow < int(m_rows.size())) ? &m_rows[m_selectedRow] : nullptr;
}

QString PDFAppStatisticsTable::getTitle() const
{
    // 480:14373, 480:14555
    return isTypeView() ? tr("Statistics by object type") : tr("Statistics by object function");
}

QString PDFAppStatisticsTable::getSummary() const
{
    // "4,819,915 bytes in 10,275 objects" (480:14374), "12,406 values in 10,275 objects" (480:14555)
    using Tool = PDFDocumentToolStatistics;
    const qint64 objectCount = m_data.value(QLatin1String(Tool::DATA_OBJECT_COUNT)).toInteger();
    const QJsonValue totalValue = m_data.value(QLatin1String(isTypeView() ? Tool::DATA_VALUE_COUNT : Tool::DATA_TOTAL_BYTES));
    qint64 total = totalValue.toInteger();
    if (totalValue.isUndefined())
    {
        // Data without the total: the sum of the rows
        for (const PDFAppStatisticsRow& row : m_rows)
        {
            total += isTypeView() ? row.count : qMax<qint64>(0, row.bytes);
        }
    }

    const int count = int(qBound(qint64(0), objectCount, qint64((std::numeric_limits<int>::max)())));
    if (isTypeView())
    {
        return tr("%1 values in %Ln objects", nullptr, count).arg(PDFAppLocale::formatNumber(total));
    }
    return tr("%1 bytes in %Ln objects", nullptr, count).arg(PDFAppLocale::formatNumber(total));
}

std::vector<PDFAppStatisticsRow> PDFAppStatisticsTable::createRows(const QJsonObject& data, const QString& view)
{
    using Tool = PDFDocumentToolStatistics;

    const bool isType = view == QLatin1String(Tool::VIEW_TYPE);
    const QJsonArray array = data.value(QLatin1String(isType ? Tool::DATA_TYPES : Tool::DATA_CLASSES)).toArray();

    // The rows of the data (the share is a percent, the core computes it)
    std::vector<PDFAppStatisticsRow> rows;
    for (const QJsonValue& value : array)
    {
        const QJsonObject object = value.toObject();
        PDFAppStatisticsRow row;
        row.id = object.value(QLatin1String(isType ? Tool::ROW_TYPE : Tool::ROW_ID)).toString();
        row.title = isType ? getTypeTitle(row.id) : getClassTitle(row.id);
        row.count = qMax<qint64>(0, object.value(QLatin1String(Tool::ROW_COUNT)).toInteger());
        row.bytes = isType ? -1 : qMax<qint64>(0, object.value(QLatin1String(Tool::ROW_BYTES)).toInteger());
        const double share = object.value(QLatin1String(Tool::ROW_SHARE)).toDouble();
        row.share = std::isfinite(share) ? qBound(0.0, share, 100.0) : 0.0;
        if (row.count == 0 && row.bytes <= 0)
        {
            continue;
        }
        rows.push_back(std::move(row));
    }

    std::stable_sort(rows.begin(), rows.end(), &PDFAppStatisticsTableHelper::isLargerShare);
    return rows;
}

QString PDFAppStatisticsTable::getClassTitle(const QString& id)
{
    // One title of a class for the canvas and the export (the core, 480:14219)
    return PDFDocumentToolStatistics::getClassTitle(id);
}

QString PDFAppStatisticsTable::getTypeTitle(const QString& id)
{
    return PDFDocumentToolStatistics::getTypeTitle(id);
}

QString PDFAppStatisticsTable::getClassPluralTitle(const QString& id)
{
    // The subject of the status message (480:14255 "Images take 61.42 % of the document.")
    static const std::map<QString, const char*> titles = {
        { QStringLiteral("page"), QT_TR_NOOP("Pages") },
        { QStringLiteral("content-stream"), QT_TR_NOOP("Content streams") },
        { QStringLiteral("graphic-state"), QT_TR_NOOP("Graphic states") },
        { QStringLiteral("color-space"), QT_TR_NOOP("Color spaces") },
        { QStringLiteral("pattern"), QT_TR_NOOP("Patterns") },
        { QStringLiteral("shading"), QT_TR_NOOP("Shadings") },
        { QStringLiteral("image"), QT_TR_NOOP("Images") },
        { QStringLiteral("form"), QT_TR_NOOP("Forms") },
        { QStringLiteral("font"), QT_TR_NOOP("Fonts") },
        { QStringLiteral("action"), QT_TR_NOOP("Actions") },
        { QStringLiteral("annotation"), QT_TR_NOOP("Annotations") },
        { QStringLiteral("file-structure"), QT_TR_NOOP("The objects of the file structure") },
        { QStringLiteral("other"), QT_TR_NOOP("Other objects") }
    };
    auto it = titles.find(id);
    return it != titles.cend() ? tr(it->second) : getClassTitle(id);
}

PDFThemeColors::Color PDFAppStatisticsTable::getChartColor(int row)
{
    return static_cast<PDFThemeColors::Color>(PDFThemeColors::Chart1 + (qMax(0, row) % 12));
}

int PDFAppStatisticsTable::getTableTop() const
{
    return HEAD_HEIGHT + GAP + BAR_HEIGHT + GAP;
}

QSize PDFAppStatisticsTable::sizeHint() const
{
    return QSize(774, getTableTop() + TABLE_HEAD_HEIGHT + int(m_rows.size()) * ROW_HEIGHT + 1);
}

QSize PDFAppStatisticsTable::minimumSizeHint() const
{
    return QSize(NAME_WIDTH + SHARE_WIDTH + COUNT_WIDTH + BYTES_WIDTH + 6 * CELL_PADDING + 40, sizeHint().height());
}

QRect PDFAppStatisticsTable::getRowRect(int row) const
{
    if (row < 0 || row >= int(m_rows.size()))
    {
        return QRect();
    }
    return QRect(1, getTableTop() + TABLE_HEAD_HEIGHT + row * ROW_HEIGHT, width() - 2, ROW_HEIGHT);
}

QRect PDFAppStatisticsTable::getStackedBarRect() const
{
    return QRect(0, HEAD_HEIGHT + GAP, width(), BAR_HEIGHT);
}

QRect PDFAppStatisticsTable::getSegmentRect(int row) const
{
    // 480:14375: the segments 1 px apart, each with its share of the width, the smallest MINIMUM_SEGMENT wide;
    // the widths of the small ones are taken from the largest segment (the first row)
    const QRect bar = getStackedBarRect();
    const int count = int(m_rows.size());
    if (row < 0 || row >= count || bar.width() <= 0)
    {
        return QRect();
    }

    double shareSum = 0.0;
    for (const PDFAppStatisticsRow& item : m_rows)
    {
        shareSum += item.share;
    }
    const int available = bar.width() - (count - 1);
    std::vector<int> widths(count, 0);
    int used = 0;
    for (int i = 1; i < count; ++i)
    {
        const double ratio = shareSum > 0.0 ? m_rows[i].share / shareSum : 0.0;
        widths[i] = qMax(MINIMUM_SEGMENT, int(std::lround(ratio * available)));
        used += widths[i];
    }
    widths[0] = qMax(MINIMUM_SEGMENT, available - used);

    int x = bar.left();
    for (int i = 0; i < row; ++i)
    {
        x += widths[i] + 1;
    }
    const QRect logical(x, bar.top(), widths[row], bar.height());
    return QStyle::visualRect(layoutDirection(), rect(), logical.intersected(bar));
}

int PDFAppStatisticsTable::getRowAt(const QPoint& position) const
{
    for (int i = 0; i < int(m_rows.size()); ++i)
    {
        if (getRowRect(i).contains(position) || getSegmentRect(i).contains(position))
        {
            return i;
        }
    }
    return -1;
}

void PDFAppStatisticsTable::onThemeChanged()
{
    update();
}

void PDFAppStatisticsTable::mousePressEvent(QMouseEvent* event)
{
    const int row = getRowAt(event->position().toPoint());
    if (row >= 0)
    {
        event->accept();
        setFocus(Qt::MouseFocusReason);
        setSelectedRow(row);
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppStatisticsTable::keyPressEvent(QKeyEvent* event)
{
    const int count = int(m_rows.size());
    int row = m_selectedRow;
    switch (event->key())
    {
        case Qt::Key_Up:
            row = qMax(0, row - 1);
            break;
        case Qt::Key_Down:
            row = qMin(count - 1, row + 1);
            break;
        case Qt::Key_Home:
            row = 0;
            break;
        case Qt::Key_End:
            row = count - 1;
            break;
        default:
            BaseClass::keyPressEvent(event);
            return;
    }

    event->accept();
    if (count > 0)
    {
        setSelectedRow(row);
    }
}

void PDFAppStatisticsTable::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFPainterStateGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);

    // preview-bar 480:14372: the title Semi Bold 13, the summary Regular 12 at the trailing edge
    const Qt::LayoutDirection direction = layoutDirection();
    const QRect head(0, 0, width(), HEAD_HEIGHT);
    const QString summary = getSummary();
    const QFont summaryFont = PDFAppStatisticsTableHelper::font(PDFThemeFonts::Role::Small);
    const int summaryWidth = qCeil(PDFStylePainter::textWidth(summary, summaryFont));
    PDFStylePainter::text(&painter, QStyle::visualRect(direction, rect(), QRect(head.left(), head.top(), head.width() - summaryWidth - 8, head.height())),
                          QStyle::visualAlignment(direction, Qt::AlignLeft | Qt::AlignVCenter), getTitle(),
                          PDFAppStatisticsTableHelper::font(PDFThemeFonts::Role::BodySemiBold), PDFStylePainter::color(PDFThemeColors::TextPrimary));
    if (!m_rows.empty())
    {
        PDFStylePainter::text(&painter, QStyle::visualRect(direction, rect(), QRect(head.right() - summaryWidth, head.top(), summaryWidth + 1, head.height())),
                              QStyle::visualAlignment(direction, Qt::AlignRight | Qt::AlignVCenter), summary, summaryFont, PDFStylePainter::color(PDFThemeColors::TextTertiary));
    }

    paintStackedBar(&painter);
    paintTable(&painter);
}

void PDFAppStatisticsTable::paintStackedBar(QPainter* painter) const
{
    // stacked-bar 480:14375: radius 6, the segments chart/1…12
    const QRect bar = getStackedBarRect();
    PDFPainterStateGuard guard(painter);
    painter->setClipPath(PDFStylePainter::roundedPath(QRectF(bar), PDFStylePainter::Radii(6)));
    if (m_rows.empty())
    {
        PDFStylePainter::fillRect(painter, QRectF(bar), PDFThemeColors::BgSurface);
        return;
    }

    for (int i = 0; i < int(m_rows.size()); ++i)
    {
        PDFStylePainter::fillRect(painter, QRectF(getSegmentRect(i)), getChartColor(i));
    }
}

void PDFAppStatisticsTable::paintTable(QPainter* painter) const
{
    using Helper = PDFAppStatisticsTableHelper;

    // statistics-table 480:14388: a card (bg/surface, border/default, radius 10) with the head bg/subtle
    const Qt::LayoutDirection direction = layoutDirection();
    const bool isType = isTypeView();
    const int top = getTableTop();
    const QRectF card(0.5, top + 0.5, width() - 1.0, TABLE_HEAD_HEIGHT + int(m_rows.size()) * ROW_HEIGHT);
    {
        PDFPainterStateGuard guard(painter);
        PDFStylePainter::fill(painter, card, PDFStylePainter::Radii(10), PDFThemeColors::BgSurface);
        painter->setClipPath(PDFStylePainter::roundedPath(card, PDFStylePainter::Radii(10)));
        PDFStylePainter::fillRect(painter, QRectF(1, top + 1, width() - 2, TABLE_HEAD_HEIGHT - 1), PDFThemeColors::BgSubtle);

        // The chosen row bg/accent-subtle (480:14399)
        const QRect selected = getRowRect(m_selectedRow);
        if (!selected.isEmpty())
        {
            PDFStylePainter::fillRect(painter, QRectF(selected), PDFThemeColors::BgAccentSubtle);
        }
    }

    // The cells of a row: name 170 | share 90 | bar (the rest) | count 80 | size 110 (by function); 12 apart
    const int innerLeft = 1 + CELL_PADDING;
    const int innerRight = width() - 1 - CELL_PADDING;
    const int sizeLeft = isType ? innerRight : innerRight - BYTES_WIDTH;
    const int countLeft = (isType ? innerRight : sizeLeft - CELL_PADDING) - COUNT_WIDTH;
    const int shareLeft = innerLeft + NAME_WIDTH + CELL_PADDING;
    const int barLeft = shareLeft + SHARE_WIDTH + CELL_PADDING;
    const int barWidth = qMax(0, countLeft - CELL_PADDING - barLeft);
    const QRect area = rect();

    const QFont headFont = Helper::font(PDFThemeFonts::Role::CaptionSemiBold);
    const QColor headColor = PDFStylePainter::color(PDFThemeColors::TextTertiary);
    const int headTop = top + 1;
    const int headHeight = TABLE_HEAD_HEIGHT - 1;
    const int left = QStyle::visualAlignment(direction, Qt::AlignLeft | Qt::AlignVCenter);
    const int right = QStyle::visualAlignment(direction, Qt::AlignRight | Qt::AlignVCenter);
    PDFStylePainter::text(painter, QStyle::visualRect(direction, area, QRect(innerLeft, headTop, NAME_WIDTH, headHeight)), left, isType ? tr("Type") : tr("Class"), headFont, headColor);
    PDFStylePainter::text(painter, QStyle::visualRect(direction, area, QRect(shareLeft, headTop, SHARE_WIDTH, headHeight)), right, tr("Share (%)"), headFont, headColor);
    PDFStylePainter::text(painter, QStyle::visualRect(direction, area, QRect(countLeft, headTop, COUNT_WIDTH, headHeight)), right, tr("Count"), headFont, headColor);
    if (!isType)
    {
        PDFStylePainter::text(painter, QStyle::visualRect(direction, area, QRect(sizeLeft, headTop, BYTES_WIDTH, headHeight)), right, tr("Size (bytes)"), headFont, headColor);
    }

    const QFont nameFont = Helper::font(PDFThemeFonts::Role::SmallMedium);
    const QFont valueFont = Helper::font(PDFThemeFonts::Role::Small);
    const QColor textColor = PDFStylePainter::color(PDFThemeColors::TextPrimary);
    for (int i = 0; i < int(m_rows.size()); ++i)
    {
        const PDFAppStatisticsRow& row = m_rows[i];
        const QRect rowRect = getRowRect(i);

        // The divider above each row (border/divider)
        PDFStylePainter::fillRect(painter, QRectF(rowRect.left(), rowRect.top(), rowRect.width(), 1), PDFThemeColors::BorderDivider);
        const int cellTop = rowRect.top() + 1;
        const int cellHeight = rowRect.height() - 1;

        // The swatch 10 × 10, radius 3, its border; the name 8 after it
        const QRect swatch = QStyle::visualRect(direction, area, QRect(innerLeft, cellTop + (cellHeight - 10) / 2, 10, 10));
        PDFStylePainter::fill(painter, QRectF(swatch), PDFStylePainter::Radii(3), getChartColor(i));
        PDFStylePainter::strokeInside(painter, QRectF(swatch), PDFStylePainter::Radii(3), PDFThemeColors::BorderDefault);
        PDFStylePainter::text(painter, QStyle::visualRect(direction, area, QRect(innerLeft + 18, cellTop, NAME_WIDTH - 18, cellHeight)), left | Qt::TextSingleLine, row.title, nameFont, textColor);

        PDFStylePainter::text(painter, QStyle::visualRect(direction, area, QRect(shareLeft, cellTop, SHARE_WIDTH, cellHeight)), right, Helper::formatShare(row.share), valueFont, textColor);

        // The bar of the share: share % of the cell, at least MINIMUM_CELL_BAR, radius 3, height 8
        if (barWidth > 0)
        {
            const int valueWidth = qBound(MINIMUM_CELL_BAR, int(std::lround(row.share / 100.0 * barWidth)), barWidth);
            const QRect bar = QStyle::visualRect(direction, area, QRect(barLeft, cellTop + (cellHeight - 8) / 2, valueWidth, 8));
            PDFStylePainter::fill(painter, QRectF(bar), PDFStylePainter::Radii(3), getChartColor(i));
        }

        PDFStylePainter::text(painter, QStyle::visualRect(direction, area, QRect(countLeft, cellTop, COUNT_WIDTH, cellHeight)), right, PDFAppLocale::formatNumber(row.count), valueFont, textColor);
        if (!isType)
        {
            PDFStylePainter::text(painter, QStyle::visualRect(direction, area, QRect(sizeLeft, cellTop, BYTES_WIDTH, cellHeight)), right, PDFAppLocale::formatNumber(row.bytes), valueFont, textColor);
        }
    }

    PDFStylePainter::strokeInside(painter, card.adjusted(-0.5, -0.5, 0.5, 0.5), PDFStylePainter::Radii(10), PDFThemeColors::BorderDefault);

    // The focus of the keyboard: the ring of the chosen row
    if (hasFocus() && m_selectedRow >= 0)
    {
        PDFStylePainter::focusRing(painter, QRectF(getRowRect(m_selectedRow)).adjusted(2, 2, -2, -2), PDFStylePainter::Radii(6), false);
    }
}

}   // namespace pdf

#include "moc_pdfappstatisticstable.cpp"
