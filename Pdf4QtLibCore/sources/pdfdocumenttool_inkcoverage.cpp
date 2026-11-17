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

#include "pdfdocumenttool_inkcoverage.h"
#include "pdfcatalog.h"
#include "pdfexecutionpolicy.h"
#include "pdfoperationcontrol.h"
#include "pdfrenderer.h"

#include <QFileInfo>
#include <QJsonValue>
#include <QLocale>

#include <algorithm>

namespace pdf
{

/// The helpers of Ink Coverage (file local)
class PDFDocumentToolInkCoverageHelper
{
public:
    /// What a run knows about one page of the selection
    class PageRecord
    {
    public:
        bool isMeasured = false;
        QString errorMessage;
        bool isTrimBox = false;
        double boxArea = 0.0;
        double pageArea = 0.0;
        double maximumCoverage = 0.0;
        std::vector<double> areas;
        std::vector<double> ratios;
        std::vector<QString> issues;
    };

    /// The row of a measured page (an object of DATA_PAGES)
    static QJsonObject createRow(PDFInteger pageIndex, const PageRecord& record)
    {
        QJsonArray areas;
        QJsonArray ratios;
        double coverage = 0.0;
        for (size_t i = 0; i < record.areas.size(); ++i)
        {
            areas.append(record.areas[i]);
            ratios.append(record.ratios[i]);
            coverage += record.ratios[i];
        }

        QJsonObject row;
        row.insert(QStringLiteral("page"), qint64(pageIndex + 1));
        row.insert(QStringLiteral("box"), record.isTrimBox ? QStringLiteral("trim") : QStringLiteral("crop"));
        row.insert(QStringLiteral("boxArea"), record.boxArea);
        row.insert(QStringLiteral("pageArea"), record.pageArea);
        row.insert(QStringLiteral("area"), areas);
        row.insert(QStringLiteral("ratio"), ratios);
        row.insert(QStringLiteral("coverage"), coverage);
        row.insert(QStringLiteral("maximumCoverage"), record.maximumCoverage);
        return row;
    }

    /// The object of an ink of DATA_INKS
    static QJsonObject createInk(const PDFOutputPreviewInk& ink)
    {
        QJsonObject object;
        object.insert(QStringLiteral("name"), QString::fromLatin1(ink.name));
        object.insert(QStringLiteral("displayName"), ink.displayName);
        object.insert(QStringLiteral("isSpot"), ink.isSpot);
        object.insert(QStringLiteral("color"), ink.color.isValid() ? ink.color.name(QColor::HexRgb) : QString());
        return object;
    }

    /// A value of an array of a row (0 for a missing or wrong one: the data are untrusted for an export of MCP)
    static double getValue(const QJsonArray& array, qsizetype index)
    {
        const QJsonValue value = index < array.size() ? array.at(index) : QJsonValue();
        return value.isDouble() ? value.toDouble() : 0.0;
    }
};

void PDFInkCoverageCollector::begin(QJsonArray inks)
{
    QMutexLocker locker(&m_mutex);
    m_inks = std::move(inks);
    m_pages.clear();
}

void PDFInkCoverageCollector::addPage(QJsonObject page)
{
    QMutexLocker locker(&m_mutex);
    m_pages.push_back(std::move(page));
}

QJsonArray PDFInkCoverageCollector::getInks() const
{
    QMutexLocker locker(&m_mutex);
    return m_inks;
}

QJsonArray PDFInkCoverageCollector::getPages() const
{
    std::vector<QJsonObject> pages;
    {
        QMutexLocker locker(&m_mutex);
        pages = m_pages;
    }

    // The pages come from the threads in any order: the table has the order of the pages
    std::stable_sort(pages.begin(), pages.end(), [](const QJsonObject& left, const QJsonObject& right)
    {
        return left.value(QStringLiteral("page")).toInteger() < right.value(QStringLiteral("page")).toInteger();
    });

    QJsonArray result;
    for (QJsonObject& page : pages)
    {
        result.append(std::move(page));
    }
    return result;
}

int PDFInkCoverageCollector::getPageCount() const
{
    QMutexLocker locker(&m_mutex);
    return int(m_pages.size());
}

QString PDFDocumentToolInkCoverage::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolInkCoverage::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages, whose inks are measured")));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_RESOLUTION), QT_TR_NOOP("Resolution"), QT_TR_NOOP("Pixels of the longer side of a measured page"),
                                                    DEFAULT_RESOLUTION, MINIMUM_RESOLUTION, PDFOutputPreviewSettings::MAXIMUM_RESOLUTION));
    definitions.push_back(PDFDocumentToolTableWriter::createFormatParameter({ PDFDocumentToolTableFormat::None, PDFDocumentToolTableFormat::Csv, PDFDocumentToolTableFormat::Text,
                                                                              PDFDocumentToolTableFormat::Html, PDFDocumentToolTableFormat::Xml }, PDFDocumentToolTableFormat::None));
    return definitions;
}

PDFDocumentToolResult PDFDocumentToolInkCoverage::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    return measure(context, parameters, control, progress, nullptr);
}

PDFDocumentToolResult PDFDocumentToolInkCoverage::measure(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress, PDFInkCoverageCollector* collector)
{
    using Helper = PDFDocumentToolInkCoverageHelper;

    const PDFDocument* document = context.getDocument();
    if (!document)
    {
        return PDFDocumentToolResult::createFailed(tr("There is no document."));
    }

    QString errorMessage;
    const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(QString::fromLatin1(PARAMETER_PAGES)), &errorMessage);
    if (!selection)
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    const std::vector<PDFInteger> pages = selection->getPageIndices(document, &errorMessage);
    if (pages.empty())
    {
        return PDFDocumentToolResult::createFailed(errorMessage);
    }

    const int resolution = qBound(MINIMUM_RESOLUTION, parameters.value(QString::fromLatin1(PARAMETER_RESOLUTION), DEFAULT_RESOLUTION).toInt(), PDFOutputPreviewSettings::MAXIMUM_RESOLUTION);

    // The page as printing (Q7.8 a), the color management of the run, every ink shown
    PDFOutputPreviewSettings settings = PDFOutputPreviewSettings::create(document, context.rendering);
    settings.resolution = resolution;
    settings.isSeparationSimulated = true;

    // The objects of the rendering belong to this run and to its thread; the threads of the pages
    // only read them (PDFOutputPreviewContext) and end before the environment dies
    const PDFOutputPreviewEnvironment environment(document, settings, pages, control);
    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    const PDFOutputPreviewContext previewContext = environment.getContext();
    const std::vector<PDFOutputPreviewInk> inks = PDFOutputPreviewAnalyzer::createInks(environment.getInkMapper(), settings);

    QJsonArray inksData;
    QJsonArray spotsOverLimit;
    for (const PDFOutputPreviewInk& ink : inks)
    {
        if (ink.channel >= 0)
        {
            inksData.append(Helper::createInk(ink));
        }
        else
        {
            spotsOverLimit.append(QString::fromLatin1(ink.name));
        }
    }

    if (collector)
    {
        collector->begin(inksData);
    }

    if (progress)
    {
        progress->start(pages.size(), ProgressStartupInfo());
    }

    // The pages in parallel within the budget of the images (plan 4.5): a batch of pages, whose
    // bitmaps fit into the budget, at once; each page writes only its own record
    std::vector<Helper::PageRecord> records(pages.size());
    const qint64 budget = PDFRasterizerPool::getDefaultImageDataBudget();
    const size_t spotInkCount = environment.getInkMapper()->getActiveSpotColorCount();
    const PDFCatalog* catalog = document->getCatalog();

    auto measurePage = [&](size_t recordIndex)
    {
        Helper::PageRecord& record = records[recordIndex];
        const PDFInteger pageIndex = pages[recordIndex];
        const PDFOutputPreviewResult pageResult = PDFOutputPreviewAnalyzer::renderPage(previewContext, pageIndex, settings, control);
        if (pageResult.isCanceled)
        {
            return;
        }

        if (!pageResult.errorMessage.isEmpty())
        {
            record.errorMessage = pageResult.errorMessage;
        }
        else
        {
            record.isMeasured = true;
            record.isTrimBox = pageResult.isTrimBox;
            record.boxArea = pageResult.pageArea;
            record.pageArea = pageResult.mediaArea;
            record.maximumCoverage = pageResult.maximumCoverage;
            for (const PDFOutputPreviewInk& ink : pageResult.inks)
            {
                if (ink.channel >= 0)
                {
                    record.areas.push_back(ink.area);
                    record.ratios.push_back(ink.ratio);
                }
            }
            for (const PDFRenderError& issue : pageResult.renderingIssues)
            {
                record.issues.push_back(issue.message);
            }

            if (collector)
            {
                collector->addPage(Helper::createRow(pageIndex, record));
            }
        }

        if (progress)
        {
            progress->step();
        }
    };

    size_t batchStart = 0;
    while (batchStart < pages.size() && !PDFOperationControl::isOperationCanceled(control))
    {
        std::vector<size_t> batch;
        qint64 batchSize = 0;
        for (size_t i = batchStart; i < pages.size(); ++i)
        {
            const qint64 pageSize = PDFOutputPreviewAnalyzer::getMemoryEstimate(catalog->getPage(pages[i]), resolution, spotInkCount);
            if (!batch.empty() && batchSize + pageSize > budget)
            {
                break;
            }

            batchSize += pageSize;
            batch.push_back(i);
        }

        PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, batch.cbegin(), batch.cend(), measurePage);
        batchStart += batch.size();
    }

    if (progress)
    {
        progress->finish();
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // The data: each measured page once in the totals (B163); a page, which cannot be rendered,
    // is left out with its reason (Q7.9 b)
    QJsonArray rows;
    QJsonArray renderingIssues;
    QJsonArray unmeasuredPages;
    std::vector<double> totalAreas(size_t(inksData.size()), 0.0);
    std::vector<double> totalRatioAreas(size_t(inksData.size()), 0.0);
    double totalBoxArea = 0.0;
    double totalPageArea = 0.0;
    PDFInteger peakPage = -1;
    double peakCoverage = 0.0;
    int trimBoxCount = 0;
    int measuredCount = 0;
    PDFDocumentToolResult result;

    for (size_t i = 0; i < pages.size(); ++i)
    {
        const Helper::PageRecord& record = records[i];
        const PDFInteger pageIndex = pages[i];

        if (!record.isMeasured)
        {
            QJsonObject unmeasured;
            unmeasured.insert(QStringLiteral("page"), qint64(pageIndex + 1));
            unmeasured.insert(QStringLiteral("message"), record.errorMessage);
            unmeasuredPages.append(unmeasured);
            continue;
        }

        ++measuredCount;
        rows.append(Helper::createRow(pageIndex, record));
        for (size_t inkIndex = 0; inkIndex < totalAreas.size() && inkIndex < record.areas.size(); ++inkIndex)
        {
            totalAreas[inkIndex] += record.areas[inkIndex];
            totalRatioAreas[inkIndex] += record.ratios[inkIndex] / 100.0 * record.boxArea;
        }
        totalBoxArea += record.boxArea;
        totalPageArea += record.pageArea;
        trimBoxCount += record.isTrimBox ? 1 : 0;

        if (peakPage < 0 || record.maximumCoverage > peakCoverage)
        {
            peakPage = pageIndex;
            peakCoverage = record.maximumCoverage;
        }

        for (const QString& message : record.issues)
        {
            QJsonObject issue;
            issue.insert(QStringLiteral("page"), qint64(pageIndex + 1));
            issue.insert(QStringLiteral("message"), message);
            renderingIssues.append(issue);
            result.renderingIssues.push_back({ pageIndex, message });
        }
    }

    if (measuredCount == 0)
    {
        const QString reason = records.front().errorMessage;
        return PDFDocumentToolResult::createFailed(tr("No page could be rendered: %1").arg(reason));
    }

    QJsonArray totalAreasData;
    QJsonArray totalRatiosData;
    for (size_t i = 0; i < totalAreas.size(); ++i)
    {
        totalAreasData.append(totalAreas[i]);
        totalRatiosData.append(totalBoxArea > 0.0 ? totalRatioAreas[i] / totalBoxArea * 100.0 : 0.0);
    }

    QJsonObject total;
    total.insert(QStringLiteral("area"), totalAreasData);
    total.insert(QStringLiteral("ratio"), totalRatiosData);
    total.insert(QStringLiteral("boxArea"), totalBoxArea);
    total.insert(QStringLiteral("pageArea"), totalPageArea);

    QJsonObject peak;
    peak.insert(QStringLiteral("page"), qint64(peakPage + 1));
    peak.insert(QStringLiteral("coverage"), peakCoverage);

    const QString box = (trimBoxCount == measuredCount) ? QStringLiteral("trim") : ((trimBoxCount == 0) ? QStringLiteral("crop") : QStringLiteral("mixed"));

    result.data.insert(QString::fromLatin1(DATA_INKS), inksData);
    result.data.insert(QString::fromLatin1(DATA_PAGES), rows);
    result.data.insert(QString::fromLatin1(DATA_TOTAL), total);
    result.data.insert(QString::fromLatin1(DATA_PEAK), peak);
    result.data.insert(QString::fromLatin1(DATA_BOX), box);
    result.data.insert(QString::fromLatin1(DATA_SPOTS_OVER_LIMIT), spotsOverLimit);
    result.data.insert(QString::fromLatin1(DATA_RENDERING_ISSUES), renderingIssues);
    result.data.insert(QString::fromLatin1(DATA_UNMEASURED_PAGES), unmeasuredPages);
    result.data.insert(QString::fromLatin1(DATA_PAGE_COUNT), qint64(measuredCount));
    result.data.insert(QString::fromLatin1(DATA_RESOLUTION), qint64(resolution));

    // The facts of the result (Summary of 478:12696): machine-readable (MCP), so the numbers are in the C
    // locale - %n, not %Ln (the GUI makes its own facts by PDFAppLocale)
    const QLocale locale = QLocale::c();
    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), tr("%n page(s)", nullptr, measuredCount) });
    result.facts.push_back({ QStringLiteral("inks"), tr("Inks"), tr("%n ink(s)", nullptr, int(inksData.size())) });
    result.facts.push_back({ QStringLiteral("peak"), tr("Peak coverage"), tr("%1 % (page %2)").arg(locale.toString(peakCoverage, 'f', 0), locale.toString(peakPage + 1)) });
    result.facts.push_back({ QStringLiteral("box"), tr("Ratio of"), box == QLatin1String("trim") ? tr("Trim box") : (box == QLatin1String("crop") ? tr("Crop box") : tr("Trim box, else crop box")) });
    if (!unmeasuredPages.isEmpty())
    {
        result.facts.push_back({ QStringLiteral("unmeasured"), tr("Not measured"), tr("%n page(s)", nullptr, int(unmeasuredPages.size())) });
    }
    if (!spotsOverLimit.isEmpty())
    {
        result.facts.push_back({ QStringLiteral("spotsOverLimit"), tr("Spot inks not measured"), tr("%n ink(s)", nullptr, int(spotsOverLimit.size())) });
    }

    const PDFDocumentToolTableFormat format = PDFDocumentToolTableWriter::getFormat(parameters);
    if (format != PDFDocumentToolTableFormat::None)
    {
        const QString baseName = QFileInfo(context.documents.front().fileName).completeBaseName();
        PDFDocumentToolOutputFile output = writeOutput(result.data, parameters, PDFDocumentToolTableWriter::Options(), (baseName.isEmpty() ? tr("Document") : baseName) + tr(" - ink coverage"), &errorMessage);
        if (output.suggestedFileName.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }
        result.outputs.push_back(std::move(output));
    }

    return result;
}

PDFDocumentToolTableSection PDFDocumentToolInkCoverage::createTables(const QJsonObject& data)
{
    using Helper = PDFDocumentToolInkCoverageHelper;
    using ValueType = PDFDocumentToolTableValueType;

    const QJsonArray inks = data.value(QString::fromLatin1(DATA_INKS)).toArray();
    const QJsonArray rows = data.value(QString::fromLatin1(DATA_PAGES)).toArray();
    const QJsonObject total = data.value(QString::fromLatin1(DATA_TOTAL)).toObject();

    PDFDocumentToolTable table;
    table.name = QStringLiteral("pages");
    table.title = tr("Ink Coverage by Page");
    table.addColumn(QStringLiteral("page"), tr("Page"), ValueType::Text);

    // B179: the element names never come from the document - the name of an ink is in the title
    for (qsizetype i = 0; i < inks.size(); ++i)
    {
        const QJsonObject ink = inks.at(i).toObject();
        QString name = ink.value(QStringLiteral("displayName")).toString();
        if (name.isEmpty())
        {
            name = ink.value(QStringLiteral("name")).toString();
        }

        const QString columnName = QStringLiteral("ink%1").arg(i + 1);
        table.addColumn(columnName + QStringLiteral("-area"), tr("%1 [mm²]").arg(name), ValueType::Real, 2);
        table.addColumn(columnName + QStringLiteral("-ratio"), tr("%1 [%]").arg(name), ValueType::Real, 2);
    }

    auto addRow = [&table, &inks](const QString& label, const QJsonArray& areas, const QJsonArray& ratios)
    {
        std::vector<QVariant> cells;
        cells.reserve(size_t(inks.size()) * 2 + 1);
        cells.push_back(label);
        for (qsizetype i = 0; i < inks.size(); ++i)
        {
            cells.push_back(Helper::getValue(areas, i));
            cells.push_back(Helper::getValue(ratios, i));
        }
        table.addRow(std::move(cells));
    };

    for (const QJsonValue& rowValue : rows)
    {
        const QJsonObject row = rowValue.toObject();
        addRow(QString::number(row.value(QStringLiteral("page")).toInteger()), row.value(QStringLiteral("area")).toArray(), row.value(QStringLiteral("ratio")).toArray());
    }

    addRow(tr("Total"), total.value(QStringLiteral("area")).toArray(), total.value(QStringLiteral("ratio")).toArray());

    PDFDocumentToolTableSection document;
    document.name = QStringLiteral("inkCoverage");
    document.title = tr("Ink Coverage");
    document.addParagraph(tr("The area of an ink is weighted by its tint (mm²); the ratio is the ink inside of the trim box (else the crop box) over the area of that box (%)."));
    document.addTable(std::move(table));
    return document;
}

PDFDocumentToolOutputFile PDFDocumentToolInkCoverage::writeOutput(const QJsonObject& data, const QVariantMap& parameters, const PDFDocumentToolTableWriter::Options& options, const QString& baseName, QString* errorMessage)
{
    const PDFDocumentToolTableFormat format = PDFDocumentToolTableWriter::getFormat(parameters);
    if (format == PDFDocumentToolTableFormat::None)
    {
        if (errorMessage)
        {
            *errorMessage = tr("No format of the export is chosen.");
        }
        return PDFDocumentToolOutputFile();
    }

    return PDFDocumentToolTableWriter::createOutput(createTables(data), format, options, baseName, errorMessage);
}

}   // namespace pdf
