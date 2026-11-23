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

#include "pdfapptoolgui_inkcoverage.h"

#include "pdfappdocument.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolvalueformatter.h"
#include "pdfappworkspace.h"
#include "pdfdocumenttoolguiregistry.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include "pdfdocument.h"
#include "pdfdocumenttool_geometry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfpainterutils.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace pdf
{

/// Helpers of the GUI of Ink Coverage (no free functions, CLAUDE.md)
class PDFAppToolGUIInkCoverageHelper
{
public:
    PDFAppToolGUIInkCoverageHelper() = delete;

    using Tool = PDFDocumentToolInkCoverage;

    /// Width of the panel of the export (panel-export 478:13429)
    static constexpr int EXPORT_PANEL_WIDTH = 330;

    /// Property of a row of the export: its format
    static constexpr const char* FORMAT_PROPERTY = "pdf4qtInkCoverageFormat";

    static QString name(const char* parameter) { return QString::fromLatin1(parameter); }

    /// A number of the locale of the user interface (O17)
    static QString formatNumber(double value, int decimals)
    {
        return PDFAppLocale::getUiLocale().toString(value, 'f', decimals);
    }

    /// "312 %"
    static QString formatPercent(double value)
    {
        return PDFAppToolGUIInkCoverage::tr("%1 %", "A percentage").arg(formatNumber(std::round(value), 0));
    }

    /// The swatch of an ink: the process inks by their tokens (Figma ink/*), a spot ink by its color
    static QColor getInkColor(const PDFAppInkCoverageTableData::Ink& ink, int index)
    {
        if (!ink.isSpot)
        {
            static constexpr PDFThemeColors::Color processColors[] = { PDFThemeColors::InkCyan, PDFThemeColors::InkMagenta, PDFThemeColors::InkYellow, PDFThemeColors::InkBlack };
            if (index >= 0 && index < 4)
            {
                return PDFStylePainter::color(processColors[index]);
            }
        }
        return ink.color.isValid() ? ink.color : PDFStylePainter::color(PDFThemeColors::BorderStrong);
    }

    static double getArrayValue(const QJsonArray& array, qsizetype index)
    {
        const QJsonValue value = index < array.size() ? array.at(index) : QJsonValue();
        return value.isDouble() ? value.toDouble() : 0.0;
    }

    /// A column of the table in the coordinates of the left-to-right layout (x of the content), mirrored for
    /// right-to-left in a viewport of \p width scrolled by \p scrollX
    static QRectF place(bool isRightToLeft, int width, int scrollX, int x, int columnWidth, int y, int height)
    {
        const int left = x - scrollX;
        return isRightToLeft ? QRectF(width - left - columnWidth, y, columnWidth, height) : QRectF(left, y, columnWidth, height);
    }

    /// The leading edge of a column of the table (0 = Page, then mm² and % of each ink)
    static int getColumnX(int column)
    {
        using Table = PDFAppInkCoverageTable;
        if (column == 0)
        {
            return Table::PADDING;
        }
        const int inkIndex = (column - 1) / 2;
        return Table::PADDING + Table::PAGE_WIDTH + inkIndex * (Table::AREA_WIDTH + Table::RATIO_WIDTH) + (((column - 1) % 2 == 0) ? 0 : Table::AREA_WIDTH);
    }

    /// The width of the text of a column (the values right-aligned with a gap before the next column)
    static int getColumnWidth(int column)
    {
        using Table = PDFAppInkCoverageTable;
        return column == 0 ? Table::PAGE_WIDTH : (((column - 1) % 2 == 0) ? Table::AREA_WIDTH - 8 : Table::RATIO_WIDTH - 4);
    }

    static QJsonArray toArray(const std::vector<double>& values)
    {
        QJsonArray array;
        for (double value : values)
        {
            array.append(value);
        }
        return array;
    }
};

// ------------------------------------------------------------------ PDFAppInkCoverageTool

PDFAppInkCoverageTool::PDFAppInkCoverageTool(std::shared_ptr<PDFInkCoverageCollector> collector) :
    m_collector(std::move(collector))
{

}

PDFDocumentToolResult PDFAppInkCoverageTool::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    // The collector of this run only (the tool lives as long as its run holds it)
    return measure(context, parameters, control, progress, m_collector.get());
}

// ------------------------------------------------------------------ PDFAppInkCoverageTableData

int PDFAppInkCoverageTableData::getProcessCount() const
{
    int count = 0;
    for (const Ink& ink : inks)
    {
        count += ink.isSpot ? 0 : 1;
    }
    return count;
}

int PDFAppInkCoverageTableData::getSpotCount() const
{
    return int(inks.size()) - getProcessCount();
}

PDFAppInkCoverageTableData::Row PDFAppInkCoverageTableData::readRow(const QJsonObject& object, size_t inkCount)
{
    using H = PDFAppToolGUIInkCoverageHelper;

    Row row;
    row.page = object.value(QStringLiteral("page")).toInteger();
    row.state = RowState::Measured;
    row.boxArea = object.value(QStringLiteral("boxArea")).toDouble();
    row.maximumCoverage = object.value(QStringLiteral("maximumCoverage")).toDouble();
    const QJsonArray areas = object.value(QStringLiteral("area")).toArray();
    const QJsonArray ratios = object.value(QStringLiteral("ratio")).toArray();
    for (size_t i = 0; i < inkCount; ++i)
    {
        row.areas.push_back(H::getArrayValue(areas, qsizetype(i)));
        row.ratios.push_back(H::getArrayValue(ratios, qsizetype(i)));
    }
    return row;
}

PDFAppInkCoverageTableData PDFAppInkCoverageTableData::fromResult(const QJsonObject& data)
{
    using H = PDFAppToolGUIInkCoverageHelper;
    using Tool = PDFDocumentToolInkCoverage;

    PDFAppInkCoverageTableData result;
    for (const QJsonValue& value : data.value(QString::fromLatin1(Tool::DATA_INKS)).toArray())
    {
        const QJsonObject object = value.toObject();
        Ink ink;
        ink.name = object.value(QStringLiteral("displayName")).toString();
        if (ink.name.isEmpty())
        {
            ink.name = object.value(QStringLiteral("name")).toString();
        }
        ink.color = QColor(object.value(QStringLiteral("color")).toString());
        ink.isSpot = object.value(QStringLiteral("isSpot")).toBool();
        result.inks.push_back(ink);
    }

    // The measured pages, the pages with the errors of the rendering (Q7.9 b) and the unmeasured ones
    std::map<PDFInteger, QString> issues;
    for (const QJsonValue& value : data.value(QString::fromLatin1(Tool::DATA_RENDERING_ISSUES)).toArray())
    {
        const QJsonObject object = value.toObject();
        QString& message = issues[object.value(QStringLiteral("page")).toInteger()];
        message = message.isEmpty() ? object.value(QStringLiteral("message")).toString() : message;
    }
    // The rows by their pages (a map keeps the order of the pages, no sort with a comparator)
    std::map<PDFInteger, Row> rows;
    for (const QJsonValue& value : data.value(QString::fromLatin1(Tool::DATA_PAGES)).toArray())
    {
        Row row = readRow(value.toObject(), result.inks.size());
        auto it = issues.find(row.page);
        if (it != issues.cend())
        {
            row.state = RowState::Issue;
            row.message = it->second;
        }
        rows[row.page] = std::move(row);
        ++result.measuredCount;
    }
    for (const QJsonValue& value : data.value(QString::fromLatin1(Tool::DATA_UNMEASURED_PAGES)).toArray())
    {
        const QJsonObject object = value.toObject();
        Row row;
        row.page = object.value(QStringLiteral("page")).toInteger();
        row.state = RowState::Unmeasured;
        row.message = object.value(QStringLiteral("message")).toString();
        rows[row.page] = std::move(row);
    }
    for (auto& item : rows)
    {
        result.rows.push_back(std::move(item.second));
    }

    const QJsonObject total = data.value(QString::fromLatin1(Tool::DATA_TOTAL)).toObject();
    const QJsonArray totalAreas = total.value(QStringLiteral("area")).toArray();
    const QJsonArray totalRatios = total.value(QStringLiteral("ratio")).toArray();
    for (size_t i = 0; i < result.inks.size(); ++i)
    {
        result.totalAreas.push_back(H::getArrayValue(totalAreas, qsizetype(i)));
        result.totalRatios.push_back(H::getArrayValue(totalRatios, qsizetype(i)));
    }

    const QJsonObject peak = data.value(QString::fromLatin1(Tool::DATA_PEAK)).toObject();
    if (!peak.isEmpty() && result.measuredCount > 0)
    {
        result.peakPage = peak.value(QStringLiteral("page")).toInteger();
        result.peakCoverage = peak.value(QStringLiteral("coverage")).toDouble();
    }
    return result;
}

PDFAppInkCoverageTableData PDFAppInkCoverageTableData::fromPages(const QJsonArray& inks, const QJsonArray& pages, const std::vector<PDFInteger>& pageIndices)
{
    QJsonObject data;
    data.insert(QString::fromLatin1(PDFDocumentToolInkCoverage::DATA_INKS), inks);
    PDFAppInkCoverageTableData result = fromResult(data);

    // The finished pages by their numbers, the others pending
    std::map<PDFInteger, Row> finished;
    for (const QJsonValue& value : pages)
    {
        Row row = readRow(value.toObject(), result.inks.size());
        finished[row.page] = std::move(row);
    }

    // The totals as the core sums them: each page once (B163); the ratio is the ink inside of the boxes of
    // the measured pages over their area (R7-8)
    std::vector<double> ratioAreas(result.inks.size(), 0.0);
    result.totalAreas.assign(result.inks.size(), 0.0);
    double boxArea = 0.0;
    for (PDFInteger pageIndex : pageIndices)
    {
        auto it = finished.find(pageIndex + 1);
        if (it == finished.end())
        {
            Row row;
            row.page = pageIndex + 1;
            result.rows.push_back(std::move(row));
            continue;
        }

        const Row& row = it->second;
        for (size_t i = 0; i < result.inks.size(); ++i)
        {
            result.totalAreas[i] += row.areas[i];
            ratioAreas[i] += row.ratios[i] / 100.0 * row.boxArea;
        }
        boxArea += row.boxArea;
        if (result.peakPage < 0 || row.maximumCoverage > result.peakCoverage)
        {
            result.peakPage = row.page;
            result.peakCoverage = row.maximumCoverage;
        }
        ++result.measuredCount;
        result.rows.push_back(row);
    }

    result.totalRatios.clear();
    for (double ratioArea : ratioAreas)
    {
        result.totalRatios.push_back(boxArea > 0.0 ? ratioArea / boxArea * 100.0 : 0.0);
    }
    return result;
}

QJsonObject PDFAppInkCoverageTableData::toData() const
{
    using H = PDFAppToolGUIInkCoverageHelper;
    using Tool = PDFDocumentToolInkCoverage;

    QJsonArray inksData;
    for (const Ink& ink : inks)
    {
        QJsonObject object;
        object.insert(QStringLiteral("name"), ink.name);
        object.insert(QStringLiteral("displayName"), ink.name);
        object.insert(QStringLiteral("isSpot"), ink.isSpot);
        object.insert(QStringLiteral("color"), ink.color.isValid() ? ink.color.name(QColor::HexRgb) : QString());
        inksData.append(object);
    }

    QJsonArray rowsData;
    for (const Row& row : rows)
    {
        if (row.state == RowState::Measured || row.state == RowState::Issue)
        {
            QJsonObject object;
            object.insert(QStringLiteral("page"), qint64(row.page));
            object.insert(QStringLiteral("boxArea"), row.boxArea);
            object.insert(QStringLiteral("area"), H::toArray(row.areas));
            object.insert(QStringLiteral("ratio"), H::toArray(row.ratios));
            object.insert(QStringLiteral("maximumCoverage"), row.maximumCoverage);
            rowsData.append(object);
        }
    }

    QJsonObject total;
    total.insert(QStringLiteral("area"), H::toArray(totalAreas));
    total.insert(QStringLiteral("ratio"), H::toArray(totalRatios));

    QJsonObject data;
    data.insert(QString::fromLatin1(Tool::DATA_INKS), inksData);
    data.insert(QString::fromLatin1(Tool::DATA_PAGES), rowsData);
    data.insert(QString::fromLatin1(Tool::DATA_TOTAL), total);
    if (peakPage > 0)
    {
        QJsonObject peak;
        peak.insert(QStringLiteral("page"), qint64(peakPage));
        peak.insert(QStringLiteral("coverage"), peakCoverage);
        data.insert(QString::fromLatin1(Tool::DATA_PEAK), peak);
    }
    data.insert(QString::fromLatin1(Tool::DATA_PAGE_COUNT), qint64(measuredCount));
    return data;
}

// ------------------------------------------------------------------ PDFAppInkCoverageTable

PDFAppInkCoverageTable::PDFAppInkCoverageTable(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("inkCoverageTable"));
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    viewport()->setAutoFillBackground(false);
    setAccessibleName(tr("Ink coverage by page"));
}

PDFAppInkCoverageTable::~PDFAppInkCoverageTable() = default;

void PDFAppInkCoverageTable::setData(const PDFAppInkCoverageTableData& tableData)
{
    m_data = tableData;
    setAccessibleDescription(getAccessibleText());
    updateGeometry();
    updateScrollBars();
    viewport()->update();
}

QString PDFAppInkCoverageTable::getAccessibleText() const
{
    using RowState = PDFAppInkCoverageTableData::RowState;

    if (m_data.isEmpty())
    {
        return QString();
    }

    // The cells of a row in the order of the columns: the name of each ink with its mm² and %
    const int inkCount = int(m_data.inks.size());
    QStringList parts;
    for (int row = -1; row < int(m_data.rows.size()) && row < MAXIMUM_ACCESSIBLE_ROWS; ++row)
    {
        const QString title = row < 0 ? tr("Total") : tr("Page %1").arg(getCellText(row, 0));
        const RowState state = row < 0 ? RowState::Measured : m_data.rows[size_t(row)].state;
        switch (state)
        {
            case RowState::Pending:
                parts << tr("%1: not measured yet", "Accessible text of a pending page of Ink Coverage").arg(title);
                continue;
            case RowState::Unmeasured:
                parts << tr("%1: cannot be measured", "Accessible text of a page of Ink Coverage, which cannot be rendered").arg(title);
                continue;
            case RowState::Measured:
            case RowState::Issue:
                break;
        }

        QStringList cells;
        for (int ink = 0; ink < inkCount; ++ink)
        {
            cells << tr("%1 %2 mm², %3 %", "Accessible text of the cells of an ink: the ink, the area, the ratio")
                     .arg(m_data.inks[size_t(ink)].name, getCellText(row, 1 + 2 * ink), getCellText(row, 2 + 2 * ink));
        }
        QString text = tr("%1: %2", "Accessible text of a row of Ink Coverage: the row, its cells").arg(title, cells.join(QStringLiteral(", ")));
        if (state == RowState::Issue)
        {
            text += tr(" (measured with errors of the rendering)");
        }
        parts << text;
    }

    const int restCount = int(m_data.rows.size()) - MAXIMUM_ACCESSIBLE_ROWS;
    if (restCount > 0)
    {
        parts << tr("%Ln more pages", "Accessible text of Ink Coverage: the pages after the named ones", restCount);
    }
    return parts.join(QStringLiteral("; "));
}

int PDFAppInkCoverageTable::getContentWidth() const
{
    return PADDING + PAGE_WIDTH + int(m_data.inks.size()) * (AREA_WIDTH + RATIO_WIDTH) + PADDING;
}

QSize PDFAppInkCoverageTable::sizeHint() const
{
    const int height = INK_LINE + UNIT_LINE + int(m_data.rows.size()) * ROW_HEIGHT + TOTAL_HEIGHT + 2;
    return QSize(getContentWidth(), height);
}

QSize PDFAppInkCoverageTable::minimumSizeHint() const
{
    return QSize(PADDING * 2 + PAGE_WIDTH, INK_LINE + UNIT_LINE + ROW_HEIGHT + TOTAL_HEIGHT + 2);
}

void PDFAppInkCoverageTable::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateScrollBars();
}

void PDFAppInkCoverageTable::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    viewport()->update();
}

bool PDFAppInkCoverageTable::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::Paint)
    {
        paintEvent(static_cast<QPaintEvent*>(event));
        return true;
    }
    return BaseClass::viewportEvent(event);
}

void PDFAppInkCoverageTable::updateScrollBars()
{
    // The rows scroll between the head and the row Total
    const int rowsHeight = qMax(0, viewport()->height() - INK_LINE - UNIT_LINE - TOTAL_HEIGHT);
    const int contentHeight = int(m_data.rows.size()) * ROW_HEIGHT;
    verticalScrollBar()->setRange(0, qMax(0, contentHeight - rowsHeight));
    verticalScrollBar()->setPageStep(rowsHeight);
    verticalScrollBar()->setSingleStep(ROW_HEIGHT);
    horizontalScrollBar()->setRange(0, qMax(0, getContentWidth() - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
}

QString PDFAppInkCoverageTable::getValueText(double value, int decimals) const
{
    return PDFAppToolGUIInkCoverageHelper::formatNumber(decimals == 0 ? std::round(value) : value, decimals);
}

QString PDFAppInkCoverageTable::getCellText(int row, int column) const
{
    using RowState = PDFAppInkCoverageTableData::RowState;

    const bool isTotal = row < 0;
    if (column == 0)
    {
        return isTotal ? tr("Total") : (row < int(m_data.rows.size()) ? PDFAppLocale::formatNumber(m_data.rows[size_t(row)].page) : QString());
    }

    const size_t inkIndex = size_t(column - 1) / 2;
    const bool isArea = (column - 1) % 2 == 0;
    if (inkIndex >= m_data.inks.size())
    {
        return QString();
    }

    if (isTotal)
    {
        // 478:12696: the row Total in whole mm² and %
        const std::vector<double>& values = isArea ? m_data.totalAreas : m_data.totalRatios;
        return inkIndex < values.size() ? getValueText(values[inkIndex], 0) : QString();
    }

    if (row >= int(m_data.rows.size()))
    {
        return QString();
    }
    const PDFAppInkCoverageTableData::Row& rowData = m_data.rows[size_t(row)];
    switch (rowData.state)
    {
        case RowState::Pending:
            return isArea ? QStringLiteral("…") : QString();
        case RowState::Unmeasured:
            return isArea ? QStringLiteral("–") : QString();
        case RowState::Measured:
        case RowState::Issue:
            break;
    }

    const std::vector<double>& values = isArea ? rowData.areas : rowData.ratios;
    return inkIndex < values.size() ? getValueText(values[inkIndex], isArea ? 2 : 0) : QString();
}

void PDFAppInkCoverageTable::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    using H = PDFAppToolGUIInkCoverageHelper;
    using RowState = PDFAppInkCoverageTableData::RowState;

    QPainter painter(viewport());
    const QRectF bounds = QRectF(viewport()->rect());
    const PDFStylePainter::Radii radii(RADIUS);

    // The card (border/default, bg/surface)
    PDFStylePainter::fill(&painter, bounds, radii, PDFThemeColors::BgSurface);

    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const int scrollX = horizontalScrollBar()->value();
    const int scrollY = verticalScrollBar()->value();
    const int width = viewport()->width();

    const int leadingAlignment = int(isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute | Qt::AlignVCenter;
    const int trailingAlignment = int(isRightToLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignAbsolute | Qt::AlignVCenter;

    const QFont headFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption);
    const QFont cellFont = PDFThemeFonts::getFont(QFont::Normal, 12);
    const QFont totalFont = PDFThemeFonts::getFont(QFont::DemiBold, 12);
    const QColor primary = PDFStylePainter::color(PDFThemeColors::TextPrimary);
    const QColor secondary = PDFStylePainter::color(PDFThemeColors::TextSecondary);
    const QColor tertiary = PDFStylePainter::color(PDFThemeColors::TextTertiary);
    const QColor muted = PDFStylePainter::color(PDFThemeColors::TextMuted);
    const QColor line = PDFStylePainter::color(PDFThemeColors::BorderDefault);

    const int columnCount = 1 + 2 * int(m_data.inks.size());

    // The rows between the head and the row Total (only the shown ones)
    const int headHeight = INK_LINE + UNIT_LINE;
    const int rowsBottom = qMax(headHeight, viewport()->height() - TOTAL_HEIGHT);
    {
        PDFPainterStateGuard guard(&painter);
        painter.setClipRect(QRect(0, headHeight, width, rowsBottom - headHeight));
        const int firstRow = qMax(0, scrollY / ROW_HEIGHT);
        const int lastRow = qMin(int(m_data.rows.size()) - 1, (scrollY + rowsBottom - headHeight) / ROW_HEIGHT);
        for (int row = firstRow; row <= lastRow; ++row)
        {
            const int y = headHeight + row * ROW_HEIGHT - scrollY;
            PDFStylePainter::fillRect(&painter, QRectF(0, y, width, 1), line);
            const PDFAppInkCoverageTableData::Row& rowData = m_data.rows[size_t(row)];
            const bool isPending = rowData.state == RowState::Pending || rowData.state == RowState::Unmeasured;
            for (int column = 0; column < columnCount; ++column)
            {
                const QColor color = (column != 0 && isPending) ? muted : primary;
                const QRectF rect = H::place(isRightToLeft, width, scrollX, H::getColumnX(column), H::getColumnWidth(column), y, ROW_HEIGHT);
                PDFStylePainter::text(&painter, rect, column == 0 ? leadingAlignment : trailingAlignment, getCellText(row, column), cellFont, color);
            }

            // 478:14933: the warning icon after the number of a page with errors of the rendering
            if (rowData.state == RowState::Issue || rowData.state == RowState::Unmeasured)
            {
                const qreal numberWidth = PDFStylePainter::textWidth(getCellText(row, 0), cellFont);
                const QRectF icon = H::place(isRightToLeft, width, scrollX, PADDING + int(std::ceil(numberWidth)) + 4, 12, y + (ROW_HEIGHT - 12) / 2, 12);
                PDFStylePainter::icon(&painter, icon, QStringLiteral("alert-triangle"), rowData.state == RowState::Issue ? PDFThemeColors::AccentAmberIcon : PDFThemeColors::AccentRedIcon);
            }
        }
    }

    // The head: the swatch and the name of each ink over its two columns, the units below
    const QFontMetrics headMetrics(headFont);
    PDFStylePainter::text(&painter, H::place(isRightToLeft, width, scrollX, H::getColumnX(0), PAGE_WIDTH, INK_LINE, UNIT_LINE), leadingAlignment, tr("Page"), headFont, secondary);
    for (size_t i = 0; i < m_data.inks.size(); ++i)
    {
        const int x = PADDING + PAGE_WIDTH + int(i) * (AREA_WIDTH + RATIO_WIDTH);
        const QRectF swatch = H::place(isRightToLeft, width, scrollX, x + 6, 8, 7 + (INK_LINE - 8) / 2, 8);
        PDFStylePainter::fill(&painter, swatch, PDFStylePainter::Radii(2), H::getInkColor(m_data.inks[i], int(i)));
        PDFStylePainter::text(&painter, H::place(isRightToLeft, width, scrollX, x + 20, AREA_WIDTH + RATIO_WIDTH - 20, 7, INK_LINE), leadingAlignment,
                              headMetrics.elidedText(m_data.inks[i].name, Qt::ElideRight, AREA_WIDTH + RATIO_WIDTH - 20), headFont, secondary);
        PDFStylePainter::text(&painter, H::place(isRightToLeft, width, scrollX, H::getColumnX(1 + 2 * int(i)), AREA_WIDTH - 8, INK_LINE, UNIT_LINE), trailingAlignment, tr("mm²"), headFont, tertiary);
        PDFStylePainter::text(&painter, H::place(isRightToLeft, width, scrollX, H::getColumnX(2 + 2 * int(i)), RATIO_WIDTH - 4, INK_LINE, UNIT_LINE), trailingAlignment, tr("%"), headFont, tertiary);
    }

    // The row Total
    const int totalY = rowsBottom;
    PDFStylePainter::fillRect(&painter, QRectF(0, totalY, width, 1), line);
    for (int column = 0; column < columnCount; ++column)
    {
        PDFStylePainter::text(&painter, H::place(isRightToLeft, width, scrollX, H::getColumnX(column), H::getColumnWidth(column), totalY, TOTAL_HEIGHT),
                              column == 0 ? leadingAlignment : trailingAlignment, getCellText(-1, column), totalFont, primary);
    }

    PDFStylePainter::strokeInside(&painter, bounds, radii, line);
}

// ------------------------------------------------------------------ PDFAppInkTotalsList

PDFAppInkTotalsList::PDFAppInkTotalsList(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("inkTotals"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

PDFAppInkTotalsList::~PDFAppInkTotalsList() = default;

void PDFAppInkTotalsList::setData(const PDFAppInkCoverageTableData& tableData)
{
    m_data = tableData;
    QStringList description;
    for (size_t i = 0; i < m_data.inks.size(); ++i)
    {
        description << QStringLiteral("%1: %2").arg(m_data.inks[i].name, getAreaText(int(i)));
    }
    setAccessibleDescription(description.join(QStringLiteral("; ")));
    updateGeometry();
    update();
}

QString PDFAppInkTotalsList::getAreaText(int index) const
{
    if (index < 0 || index >= int(m_data.totalAreas.size()))
    {
        return QString();
    }
    return tr("%1 mm²", "An area of an ink").arg(PDFAppToolGUIInkCoverageHelper::formatNumber(m_data.totalAreas[size_t(index)], 2));
}

QSize PDFAppInkTotalsList::sizeHint() const
{
    return QSize(287, PADDING * 2 - 6 + int(m_data.inks.size()) * ROW_HEIGHT);
}

QSize PDFAppInkTotalsList::minimumSizeHint() const
{
    return QSize(120, sizeHint().height());
}

void PDFAppInkTotalsList::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    using H = PDFAppToolGUIInkCoverageHelper;

    // section-Total per ink 478:12696: a card, the rows of 35 (the swatch 8, the name, the area, the bar of 4)
    QPainter painter(this);
    const QRectF bounds = QRectF(rect());
    const PDFStylePainter::Radii radii(10);
    PDFStylePainter::fill(&painter, bounds, radii, PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(&painter, bounds, radii, PDFThemeColors::BorderDefault);

    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const QFont nameFont = PDFThemeFonts::getFont(QFont::Normal, 12);
    const QFont valueFont = PDFThemeFonts::getFont(QFont::Medium, 12);
    const double maximum = m_data.totalAreas.empty() ? 0.0 : *std::max_element(m_data.totalAreas.cbegin(), m_data.totalAreas.cend());
    const qreal innerWidth = width() - 2 * PADDING;
    const int leading = int(isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute | Qt::AlignVCenter;
    const int trailing = int(isRightToLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignAbsolute | Qt::AlignVCenter;

    for (size_t i = 0; i < m_data.inks.size(); ++i)
    {
        const qreal y = PADDING - 3 + qreal(i) * ROW_HEIGHT;
        const QColor color = H::getInkColor(m_data.inks[i], int(i));
        const qreal swatchX = isRightToLeft ? width() - PADDING - 8 : PADDING;
        PDFStylePainter::fill(&painter, QRectF(swatchX, y + 5, 8, 8), PDFStylePainter::Radii(2), color);
        const QRectF nameRect = isRightToLeft ? QRectF(PADDING, y, innerWidth - 16, 18) : QRectF(PADDING + 16, y, innerWidth - 16, 18);
        PDFStylePainter::text(&painter, nameRect, leading, m_data.inks[i].name, nameFont, PDFStylePainter::color(PDFThemeColors::TextPrimary));
        PDFStylePainter::text(&painter, QRectF(PADDING, y, innerWidth, 18), trailing, getAreaText(int(i)), valueFont, PDFStylePainter::color(PDFThemeColors::TextPrimary));

        // The bar: the track bg/muted, the share of the largest total in the color of the ink
        const QRectF track(PADDING, y + 22, innerWidth, 4);
        PDFStylePainter::fill(&painter, track, PDFStylePainter::Radii(2), PDFThemeColors::BgMuted);
        const double share = maximum > 0.0 && i < m_data.totalAreas.size() ? qBound(0.0, m_data.totalAreas[i] / maximum, 1.0) : 0.0;
        const qreal barWidth = qMax<qreal>(share > 0.0 ? 4.0 : 0.0, innerWidth * share);
        const QRectF bar = isRightToLeft ? QRectF(track.right() - barWidth, track.top(), barWidth, 4) : QRectF(track.left(), track.top(), barWidth, 4);
        if (barWidth > 0.0)
        {
            PDFStylePainter::fill(&painter, bar, PDFStylePainter::Radii(2), color);
        }
    }
}

// ------------------------------------------------------------------ PDFAppInkCoverageCanvasPage

PDFAppInkCoverageCanvasPage::PDFAppInkCoverageCanvasPage(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("inkCoverageCanvasPage"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(0);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    // The empty state 478:11760: the badge 56 of the tool, the title Semi Bold 18, the text Regular 13 (two lines)
    m_emptyPage = new QWidget(m_stack);
    QVBoxLayout* emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(10);
    emptyLayout->addStretch(1);
    PDFIconBadge* badge = new PDFIconBadge(QStringLiteral("ink-coverage"), 56, 22, m_emptyPage);
    badge->setRadius(-1.0);
    badge->setHue(PDFAccentHue::Teal);
    emptyLayout->addWidget(badge, 0, Qt::AlignHCenter);
    m_emptyTitle = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 18, PDFThemeColors::TextPrimary, m_emptyPage);
    m_emptyTitle->setObjectName(QStringLiteral("inkCoverageEmptyTitle"));
    m_emptyTitle->setAlignment(Qt::AlignHCenter);
    emptyLayout->addWidget(m_emptyTitle);
    m_emptyText = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 13), 15, PDFThemeColors::TextSecondary, m_emptyPage);
    m_emptyText->setObjectName(QStringLiteral("inkCoverageEmptyText"));
    m_emptyText->setAlignment(Qt::AlignHCenter);
    m_emptyText->setMaximumWidth(440);
    emptyLayout->addWidget(m_emptyText, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);
    m_stack->addWidget(m_emptyPage);

    // The table and the note below it (478:12696)
    m_tablePage = new QWidget(m_stack);
    QVBoxLayout* tableLayout = new QVBoxLayout(m_tablePage);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(GAP);
    m_table = new PDFAppInkCoverageTable(m_tablePage);
    tableLayout->addWidget(m_table);
    PDFAppParagraph* note = PDFAppPageWidgets::createText(tr("mm² is the ink area weighted by tint, % its share of the page area. The Total row sums the areas of the measured pages; its % is their average share. Peak coverage is the highest sum of all inks at one point."),
                                                          PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextMuted, m_tablePage);
    note->setObjectName(QStringLiteral("inkCoverageNote"));
    tableLayout->addWidget(note);
    tableLayout->addStretch(1);
    m_stack->addWidget(m_tablePage);
}

PDFAppInkCoverageCanvasPage::~PDFAppInkCoverageCanvasPage() = default;

void PDFAppInkCoverageCanvasPage::showEmptyState(const QString& title, const QString& text)
{
    m_emptyTitle->setText(title);
    m_emptyText->setText(text);
    m_stack->setCurrentWidget(m_emptyPage);
}

void PDFAppInkCoverageCanvasPage::showTable(const PDFAppInkCoverageTableData& tableData)
{
    m_table->setData(tableData);
    m_stack->setCurrentWidget(m_tablePage);
}

bool PDFAppInkCoverageCanvasPage::isTableShown() const
{
    return m_stack && m_stack->currentWidget() == m_tablePage;
}

void PDFAppInkCoverageCanvasPage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas: bg/track
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

// ------------------------------------------------------------------ PDFAppToolGUIInkCoverage

PDFAppToolGUIInkCoverage::PDFAppToolGUIInkCoverage(QObject* parent) :
    BaseClass(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL);
    connect(m_pollTimer.data(), &QTimer::timeout, this, &PDFAppToolGUIInkCoverage::onPollTimeout);
}

PDFAppToolGUIInkCoverage::~PDFAppToolGUIInkCoverage() = default;

void PDFAppToolGUIInkCoverage::registerInkCoverageTools()
{
    const QString toolId = QString::fromLatin1(PDFDocumentToolInkCoverage::TOOL_ID);
    if (!PDFDocumentToolRegistry::isImplemented(toolId))
    {
        // The catalog (MCP, the shell) has the core tool; a run of the GUI has its own PDFAppInkCoverageTool
        PDFDocumentToolRegistry::registerTool(std::make_shared<PDFDocumentToolInkCoverage>());
    }
    if (!PDFDocumentToolGUIRegistry::getFactory(toolId))
    {
        PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<PDFDocumentToolGUIFactoryOf<PDFAppToolGUIInkCoverage>>());
    }
}

std::vector<PDFInteger> PDFAppToolGUIInkCoverage::getSelectedPages(QString* errorMessage) const
{
    using H = PDFAppToolGUIInkCoverageHelper;

    const PDFAppDocument* document = getDocument();
    const PDFAppToolParameterModel* model = getModel();
    if (!document || !document->getDocument() || !model)
    {
        return std::vector<PDFInteger>();
    }

    QString error;
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(model->getValue(H::name(H::Tool::PARAMETER_PAGES)), &error);
    std::vector<PDFInteger> pages = selection ? selection->getPageIndices(document->getDocument().data(), &error) : std::vector<PDFInteger>();
    if (errorMessage)
    {
        *errorMessage = error;
    }
    return pages;
}

void PDFAppToolGUIInkCoverage::describeToolbar(PDFAppToolPanelBuilder& builder)
{
    using H = PDFAppToolGUIInkCoverageHelper;

    // tool-actions 478:13429: Export (CSV Table…, Text…, HTML…, XML…)
    m_exportPanel = builder.addMenu(QStringLiteral("export"), QStringLiteral("download"), tr("Export"), H::EXPORT_PANEL_WIDTH);
    m_exportPanel->getButton()->setObjectName(QStringLiteral("inkCoverageExport"));
    const std::vector<std::tuple<PDFDocumentToolTableFormat, QString, QString, QString>> formats =
    {
        { PDFDocumentToolTableFormat::Csv, tr("CSV Table…"), tr("For spreadsheets, one row per page"), QStringLiteral("csv") },
        { PDFDocumentToolTableFormat::Text, tr("Text…"), tr("Plain text table"), QStringLiteral("txt") },
        { PDFDocumentToolTableFormat::Html, tr("HTML…"), tr("Table for a web browser"), QStringLiteral("html") },
        { PDFDocumentToolTableFormat::Xml, tr("XML…"), tr("Machine-readable values"), QStringLiteral("xml") }
    };
    for (const auto& [format, title, description, iconName] : formats)
    {
        PDFTileRow* row = m_exportPanel->addRow(title, description, iconName, PDFTileRow::Kind::MenuItem);
        row->setObjectName(QStringLiteral("inkCoverageExport-") + PDFDocumentToolTableWriter::getFormatId(format));
        row->setProperty(H::FORMAT_PROPERTY, int(format));
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolGUIInkCoverage::onExportRowClicked);
    }

    updateParts();
}

void PDFAppToolGUIInkCoverage::describeLeftPanel(PDFAppToolPanelBuilder& builder)
{
    using H = PDFAppToolGUIInkCoverageHelper;

    // sidebar-left 478:11760: the Page selection and the note (Q7.8 a)
    builder.addPageSelection(H::name(H::Tool::PARAMETER_PAGES), tr("Pages"));
    builder.addHint(tr("All inks are measured: the four process inks and every spot ink of the document. Layers are evaluated as for printing."));
}

void PDFAppToolGUIInkCoverage::describeRightPanel(PDFAppToolPanelBuilder& builder)
{
    // The callouts of a stopped run, of the pages with errors (Q7.9 b) and of the spot inks over the limit
    m_callout = builder.addCallout(PDFAccentHue::Amber, QStringLiteral("alert-triangle"), QString(), QString());
    m_callout->setObjectName(QStringLiteral("inkCoverageCallout"));

    // section-Summary 478:12696
    builder.addSection(tr("Summary"));
    m_summaryRows = builder.addKeyValues();
    m_summaryRows->setObjectName(QStringLiteral("inkCoverageSummary"));
    builder.endSection();

    // section-Total per ink
    m_totalsSection = builder.addSection(tr("Total per ink"));
    m_totals = new PDFAppInkTotalsList(builder.getWidget());
    builder.addWidget(m_totals);
    builder.endSection();

    updateParts();
}

QWidget* PDFAppToolGUIInkCoverage::createCanvasPage(QWidget* parent)
{
    m_canvasPage = new PDFAppInkCoverageCanvasPage(parent);
    updateParts();
    return m_canvasPage.data();
}

QString PDFAppToolGUIInkCoverage::getCancelText() const
{
    // 478:11760 … 478:17237: Close in every state - the tool changes nothing in the document
    return tr("Close");
}

QString PDFAppToolGUIInkCoverage::getConfirmText() const
{
    // 478:12696: Calculate Again, when a table is shown
    return m_content == Content::None ? tr("Calculate Coverage") : tr("Calculate Again");
}

QString PDFAppToolGUIInkCoverage::getConfirmIconName() const
{
    return QStringLiteral("ink-coverage");
}

bool PDFAppToolGUIInkCoverage::isConfirmEnabled() const
{
    // 478:17237: an invalid page selection
    QString error;
    return BaseClass::isConfirmEnabled() && !getSelectedPages(&error).empty();
}

bool PDFAppToolGUIInkCoverage::isResultKeptInTool() const
{
    return true;
}

void PDFAppToolGUIInkCoverage::prepareEnvironment(PDFAppToolRunEnvironment* environment) const
{
    BaseClass::prepareEnvironment(environment);

    // The run gives its finished pages to a new collector (the rows during the run, the pages after Stop): the
    // tool of the run carries it, so only this run reaches it (RS7APP L3)
    m_runPages = getSelectedPages();
    m_collector = std::make_shared<PDFInkCoverageCollector>();
    environment->runTool = std::make_shared<PDFAppInkCoverageTool>(m_collector);
}

void PDFAppToolGUIInkCoverage::onRunStateChanged(bool isRunning)
{
    BaseClass::onRunStateChanged(isRunning);
    m_isRunning = isRunning;

    if (isRunning)
    {
        m_contentBeforeRun = m_content;
        m_dataBeforeRun = m_tableData;
        m_resultDataBeforeRun = m_resultData;
        m_pollTimer->start();
        setContent(Content::Running, PDFAppInkCoverageTableData::fromPages(QJsonArray(), QJsonArray(), m_runPages), QJsonObject());
        return;
    }

    // The run has ended: the finished pages stay (a stopped run, 478:14194); a result or a failure follows
    // (onResultKept, onRunNotAccepted) and replaces them. The collector is dropped before the signals.
    m_pollTimer->stop();
    pollCollector();
    m_collector.reset();
    const bool hasPages = m_tableData.measuredCount > 0;
    setContent(hasPages ? Content::Stopped : m_contentBeforeRun, hasPages ? m_tableData : m_dataBeforeRun, hasPages ? QJsonObject() : m_resultDataBeforeRun);
}

void PDFAppToolGUIInkCoverage::onResultKept(const PDFDocumentToolResult& result)
{
    BaseClass::onResultKept(result);
    setContent(Content::Result, PDFAppInkCoverageTableData::fromResult(result.data), result.data);
}

void PDFAppToolGUIInkCoverage::onRunNotAccepted(const PDFDocumentToolResult& result)
{
    // A failed run shows its error (the default info bar); the table of before stays
    setContent(m_contentBeforeRun, m_dataBeforeRun, m_resultDataBeforeRun);
    if (!PDFDeferredDeletion::isRetired(this))
    {
        BaseClass::onRunNotAccepted(result);
    }
}

void PDFAppToolGUIInkCoverage::onPollTimeout()
{
    pollCollector();
    updateParts();
}

void PDFAppToolGUIInkCoverage::pollCollector()
{
    if (!m_collector || !m_isRunning)
    {
        if (m_collector && m_content == Content::Running)
        {
            m_tableData = PDFAppInkCoverageTableData::fromPages(m_collector->getInks(), m_collector->getPages(), m_runPages);
        }
        return;
    }
    m_tableData = PDFAppInkCoverageTableData::fromPages(m_collector->getInks(), m_collector->getPages(), m_runPages);
}

void PDFAppToolGUIInkCoverage::setContent(Content content, const PDFAppInkCoverageTableData& data, const QJsonObject& resultData)
{
    m_content = content;
    m_tableData = data;
    m_resultData = resultData;
    updateParts();
    announceState();
}

void PDFAppToolGUIInkCoverage::announceState()
{
    // The signals of the Primary button and of the status bar run foreign slots, which may delete this GUI
    // with its shell: each is the last call of what follows it (emit last)
    updateConfirmState();
    if (!PDFDeferredDeletion::isRetired(this))
    {
        updateStatusText();
    }
}

void PDFAppToolGUIInkCoverage::onParametersChanged(const QString& name)
{
    BaseClass::onParametersChanged(name);
    updateParts();
    announceState();
}

void PDFAppToolGUIInkCoverage::onDocumentChanged()
{
    BaseClass::onDocumentChanged();
    updateParts();
}

void PDFAppToolGUIInkCoverage::onExportRowClicked()
{
    QObject* row = sender();
    if (!row)
    {
        return;
    }

    // The panel closes first, the dialog opens as the last call
    const PDFDocumentToolTableFormat format = PDFDocumentToolTableFormat(row->property(PDFAppToolGUIInkCoverageHelper::FORMAT_PROPERTY).toInt());
    if (m_exportPanel)
    {
        m_exportPanel->close();
    }
    exportTable(format);
}

QString PDFAppToolGUIInkCoverage::getBaseName() const
{
    // 478:15674: "Brochure_Print - ink coverage"
    const PDFAppDocument* document = getDocument();
    QString title = document ? document->getTitle() : QString();
    if (title.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
    {
        title.chop(4);
    }
    return tr("%1 - ink coverage", "The name of the file of an export of Ink Coverage, 478:15674").arg(title);
}

bool PDFAppToolGUIInkCoverage::exportTable(PDFDocumentToolTableFormat format)
{
    if (m_content == Content::None || m_content == Content::Running || m_tableData.measuredCount == 0)
    {
        return false;
    }

    // The data of the shown table: the result of the run, or the finished pages of a stopped run
    const QJsonObject data = m_content == Content::Result ? m_resultData : m_tableData.toData();
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolInkCoverage::PARAMETER_FORMAT), PDFDocumentToolTableWriter::getFormatId(format));
    QString errorMessage;
    PDFDocumentToolOutputFile output = PDFDocumentToolInkCoverage::writeOutput(data, parameters, PDFAppToolValueFormatter::createOptions(PDFDocumentToolDateFormat::Short), getBaseName(), &errorMessage);
    if (output.suggestedFileName.isEmpty())
    {
        if (PDFAppToolHost* host = getHost())
        {
            host->showTransientMessage(errorMessage);
        }
        return false;
    }

    // 478:15674: Pages, Inks, Format
    QString formatText;
    switch (format)
    {
        case PDFDocumentToolTableFormat::Csv:
            formatText = tr("CSV table");
            break;
        case PDFDocumentToolTableFormat::Text:
            formatText = tr("Plain text");
            break;
        case PDFDocumentToolTableFormat::Html:
            formatText = tr("HTML");
            break;
        case PDFDocumentToolTableFormat::Xml:
            formatText = tr("XML");
            break;
        case PDFDocumentToolTableFormat::None:
            break;
    }

    PDFDocumentToolResult exportResult;
    exportResult.outputs.push_back(std::move(output));
    exportResult.facts.push_back({ QStringLiteral("pages"), tr("Pages"), tr("%1 and the Total row").arg(PDFAppLocale::formatNumber(m_tableData.measuredCount)) });
    exportResult.facts.push_back({ QStringLiteral("inks"), tr("Inks"), tr("%1 process, %2 spot").arg(PDFAppLocale::formatNumber(m_tableData.getProcessCount()), PDFAppLocale::formatNumber(m_tableData.getSpotCount())) });
    exportResult.facts.push_back({ QStringLiteral("format"), tr("Format"), formatText });
    m_exportFormat = format;
    return openExport(exportResult);
}

void PDFAppToolGUIInkCoverage::fillResultInput(PDFAppToolResultInput* input) const
{
    BaseClass::fillResultInput(input);
    input->title = tr("Ink coverage is ready to export");
    input->statusText = tr("The table is ready to be exported.");
}

void PDFAppToolGUIInkCoverage::onExportDecided(const PDFAppToolResultOutcome& outcome)
{
    BaseClass::onExportDecided(outcome);
    updateStatusText();
}

QString PDFAppToolGUIInkCoverage::getPageSizeText(const std::vector<PDFInteger>& pageIndices) const
{
    // "A4 · 62,370 mm²": the box of the ratios (TrimBox, else CropBox, clipped by MediaBox - Q7.6 b); "Mixed"
    // for pages of more sizes (FP-5)
    const PDFAppDocument* document = getDocument();
    const PDFCatalog* catalog = document && document->getDocument() ? document->getDocument()->getCatalog() : nullptr;
    if (!catalog || pageIndices.empty())
    {
        return QString();
    }

    QSizeF firstSize;
    double firstArea = 0.0;
    for (PDFInteger pageIndex : pageIndices)
    {
        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        if (!page)
        {
            continue;
        }
        const QRectF box = page->getTrimBox().intersected(page->getMediaBox());
        const QRectF boxMM = page->getTrimBoxMM().intersected(page->getMediaBoxMM());
        const QSizeF size = box.size();
        if (firstSize.isEmpty())
        {
            firstSize = size;
            firstArea = boxMM.width() * boxMM.height();
        }
        else if (std::abs(size.width() - firstSize.width()) > 0.5 || std::abs(size.height() - firstSize.height()) > 0.5)
        {
            return tr("Mixed", "The sizes of the measured pages differ");
        }
    }

    const QString paper = PDFDocumentToolResizePages::getPaperName(firstSize);
    const QString area = tr("%1 mm²", "An area").arg(PDFAppToolGUIInkCoverageHelper::formatNumber(std::round(firstArea), 0));
    return paper.isEmpty() ? area : tr("%1 · %2", "A paper and its area").arg(paper, area);
}

QString PDFAppToolGUIInkCoverage::getStatusText() const
{
    QString error;
    const std::vector<PDFInteger> pages = getSelectedPages(&error);
    if (pages.empty())
    {
        return tr("The page selection is not valid.");
    }

    const int pageCount = int(m_tableData.rows.size());
    switch (m_content)
    {
        case Content::None:
            return tr("Ready to calculate the ink coverage of %Ln pages.", "", int(pages.size()));
        case Content::Running:
            return getRunningStatusText(QString());
        case Content::Stopped:
            return tr("Stopped. %1 of %2 pages were measured.").arg(PDFAppLocale::formatNumber(m_tableData.measuredCount), PDFAppLocale::formatNumber(pageCount));
        case Content::Result:
            break;
    }

    for (const PDFAppInkCoverageTableData::Row& row : m_tableData.rows)
    {
        if (row.state == PDFAppInkCoverageTableData::RowState::Issue)
        {
            return tr("%1 of %2 pages measured, page %3 with rendering errors.").arg(PDFAppLocale::formatNumber(m_tableData.measuredCount), PDFAppLocale::formatNumber(pageCount), PDFAppLocale::formatNumber(row.page));
        }
    }
    return tr("Ink coverage of %Ln pages calculated.", "", m_tableData.measuredCount);
}

QString PDFAppToolGUIInkCoverage::getRunningStatusText(const QString& stepText) const
{
    Q_UNUSED(stepText);
    return tr("Calculating… Stop keeps the pages finished so far.");
}

QString PDFAppToolGUIInkCoverage::getRunningCardTitle(int percentage, const QString& stepText) const
{
    Q_UNUSED(percentage);
    Q_UNUSED(stepText);
    return tr("Calculating ink coverage");
}

QString PDFAppToolGUIInkCoverage::getRunningCardStepText(const QString& stepText) const
{
    Q_UNUSED(stepText);

    // 478:11954: "Page 7 of 16 · finished pages are listed as they are ready"
    const int total = int(m_runPages.size());
    const int finished = m_collector ? m_collector->getPageCount() : 0;
    return tr("Page %1 of %2 · finished pages are listed as they are ready").arg(PDFAppLocale::formatNumber(qMin(finished + 1, qMax(total, 1))), PDFAppLocale::formatNumber(total));
}

QString PDFAppToolGUIInkCoverage::getRunningTimeText(int seconds) const
{
    Q_UNUSED(seconds);
    return QString();
}

void PDFAppToolGUIInkCoverage::updateParts()
{
    using H = PDFAppToolGUIInkCoverageHelper;
    using RowState = PDFAppInkCoverageTableData::RowState;

    QString error;
    const std::vector<PDFInteger> pages = getSelectedPages(&error);
    const bool hasTable = m_content != Content::None && !m_tableData.rows.empty();
    const bool hasMeasured = m_tableData.measuredCount > 0;

    // The toolbar: Export with a measured table
    if (m_exportPanel)
    {
        m_exportPanel->getButton()->setEnabled(hasMeasured && (m_content == Content::Result || m_content == Content::Stopped));
    }

    // The canvas
    if (m_canvasPage)
    {
        if (pages.empty() && !hasTable)
        {
            m_canvasPage->showEmptyState(tr("Fix the page selection first"), tr("The page range on the left is not valid, so nothing can be calculated yet."));
        }
        else if (!hasTable)
        {
            m_canvasPage->showEmptyState(tr("Ink coverage of %Ln pages", "", int(pages.size())),
                                         tr("For every page and every ink you get the covered area in mm² and its share of the page. Nothing is changed in the document."));
        }
        else
        {
            m_canvasPage->showTable(m_tableData);
        }
    }

    // Summary: Pages, Inks, Page size, Peak coverage
    if (m_summaryRows)
    {
        m_summaryRows->clear();
        const QString pagesText = !hasTable ? tr("%Ln to measure", "", int(pages.size()))
                                            : tr("%1 of %2").arg(PDFAppLocale::formatNumber(m_tableData.measuredCount), PDFAppLocale::formatNumber(int(m_tableData.rows.size())));
        m_summaryRows->addRow(tr("Pages"), pagesText);
        const QString inksText = m_tableData.inks.empty() ? tr("Not known yet")
                                                          : tr("%1 process, %2 spot").arg(PDFAppLocale::formatNumber(m_tableData.getProcessCount()), PDFAppLocale::formatNumber(m_tableData.getSpotCount()));
        m_summaryRows->addRow(tr("Inks"), inksText);
        const QString sizeText = getPageSizeText(pages);
        if (!sizeText.isEmpty())
        {
            m_summaryRows->addRow(tr("Page size"), sizeText);
        }
        if (m_tableData.peakPage > 0)
        {
            m_summaryRows->addRow(tr("Peak coverage"), tr("Page %1 · %2").arg(PDFAppLocale::formatNumber(m_tableData.peakPage), H::formatPercent(m_tableData.peakCoverage)));
        }
    }

    // Total per ink
    const bool hasTotals = hasTable && !m_tableData.inks.empty();
    if (m_totals)
    {
        m_totals->setData(m_tableData);
        m_totals->setVisible(hasTotals);
    }
    if (m_totalsSection)
    {
        m_totalsSection->setVisible(hasTotals);
    }

    // The callout: stopped (478:14194), the pages with the errors of the rendering (478:14933), the pages,
    // which cannot be rendered (Q7.9 b)
    if (m_callout)
    {
        QString title;
        QString text;
        PDFInteger issuePage = -1;
        PDFInteger unmeasuredPage = -1;
        int issueCount = 0;
        int unmeasuredCount = 0;
        for (const PDFAppInkCoverageTableData::Row& row : m_tableData.rows)
        {
            if (row.state == RowState::Issue)
            {
                issuePage = issuePage < 0 ? row.page : issuePage;
                ++issueCount;
            }
            if (row.state == RowState::Unmeasured)
            {
                unmeasuredPage = unmeasuredPage < 0 ? row.page : unmeasuredPage;
                ++unmeasuredCount;
            }
        }

        if (m_content == Content::Stopped)
        {
            title = tr("Stopped after %1 of %2 pages").arg(PDFAppLocale::formatNumber(m_tableData.measuredCount), PDFAppLocale::formatNumber(int(m_tableData.rows.size())));
            text = tr("The table holds the pages finished so far. Calculate Again starts from the beginning.");
        }
        else if (m_content == Content::Result && unmeasuredCount > 0)
        {
            title = unmeasuredCount == 1 ? tr("Page %1 cannot be rendered").arg(PDFAppLocale::formatNumber(unmeasuredPage)) : tr("%Ln pages cannot be rendered", "", unmeasuredCount);
            text = tr("They are left out of the totals. The other pages are measured.");
        }
        else if (m_content == Content::Result && issueCount > 0)
        {
            title = issueCount == 1 ? tr("Page %1 was rendered with errors").arg(PDFAppLocale::formatNumber(issuePage)) : tr("%Ln pages were rendered with errors", "", issueCount);
            text = tr("Its values are measured and included in the totals, but may differ from print. Only a page that cannot be rendered at all is left out.");
        }
        m_callout->setTexts(title, text);
        m_callout->setVisible(!title.isEmpty());
    }
}

}   // namespace pdf

#include "moc_pdfapptoolgui_inkcoverage.cpp"
