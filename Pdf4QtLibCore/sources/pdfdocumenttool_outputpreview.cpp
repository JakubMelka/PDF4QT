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

#include "pdfdocumenttool_outputpreview.h"
#include "pdfcatalog.h"
#include "pdfoperationcontrol.h"

#include <QJsonArray>
#include <QLocale>

#include <limits>

namespace pdf
{

PDFOutputPreviewSettings PDFDocumentToolOutputPreview::createSettings(const QVariantMap& parameters)
{
    PDFOutputPreviewSettings settings;
    settings.resolution = qBound(72, parameters.value(QString::fromLatin1(PARAMETER_RESOLUTION), settings.resolution).toInt(), PDFOutputPreviewSettings::MAXIMUM_RESOLUTION);
    settings.coverageLimit = parameters.value(QString::fromLatin1(PARAMETER_COVERAGE_LIMIT), settings.coverageLimit).toDouble();
    settings.richBlackLimit = parameters.value(QString::fromLatin1(PARAMETER_RICH_BLACK_LIMIT), settings.richBlackLimit).toDouble();
    settings.isSeparationSimulated = parameters.value(QString::fromLatin1(PARAMETER_SIMULATE_SEPARATIONS), settings.isSeparationSimulated).toBool();
    settings.isPaperColorSimulated = parameters.value(QString::fromLatin1(PARAMETER_SIMULATE_PAPER_COLOR), settings.isPaperColorSimulated).toBool();

    const QVariant paperColor = parameters.value(QString::fromLatin1(PARAMETER_PAPER_COLOR));
    if (paperColor.canConvert<QColor>() && paperColor.value<QColor>().isValid())
    {
        settings.paperColor = paperColor.value<QColor>();
    }

    const QStringList inactiveInks = parameters.value(QString::fromLatin1(PARAMETER_INACTIVE_INKS)).toString().split(QLatin1Char(','));
    for (const QString& inkName : inactiveInks)
    {
        const QString trimmed = inkName.trimmed();
        if (!trimmed.isEmpty())
        {
            settings.inactiveInks.push_back(trimmed.toLatin1());
        }
    }

    const std::pair<const char*, PDFOutputPreviewContent> contents[] = {
        { PARAMETER_IMAGES, PDFOutputPreviewContent::Images },
        { PARAMETER_TEXT, PDFOutputPreviewContent::Text },
        { PARAMETER_VECTORS, PDFOutputPreviewContent::Vectors },
        { PARAMETER_SHADINGS, PDFOutputPreviewContent::Shadings },
        { PARAMETER_PATTERNS, PDFOutputPreviewContent::Patterns }
    };

    settings.content = uint32_t(PDFOutputPreviewContent::None);
    for (const auto& [name, content] : contents)
    {
        if (parameters.value(QString::fromLatin1(name), true).toBool())
        {
            settings.content |= uint32_t(content);
        }
    }

    return settings;
}

QJsonObject PDFDocumentToolOutputPreview::createData(const PDFOutputPreviewResult& result, const PDFOutputPreviewSettings& settings)
{
    QJsonArray inks;
    for (const PDFOutputPreviewInk& ink : result.inks)
    {
        QJsonObject object;
        object.insert(QStringLiteral("name"), QString::fromLatin1(ink.name));
        object.insert(QStringLiteral("displayName"), ink.displayName);
        object.insert(QStringLiteral("isSpot"), ink.isSpot);
        object.insert(QStringLiteral("isActive"), ink.isActive);
        object.insert(QStringLiteral("isMeasured"), ink.channel >= 0);
        object.insert(QStringLiteral("color"), ink.color.isValid() ? ink.color.name(QColor::HexRgb) : QString());
        object.insert(QStringLiteral("area"), ink.area);
        object.insert(QStringLiteral("ratio"), ink.ratio);
        inks.append(object);
    }

    QJsonArray issues;
    for (const PDFRenderError& issue : result.renderingIssues)
    {
        issues.append(issue.message);
    }

    QJsonObject data;
    data.insert(QStringLiteral("page"), qint64(result.pageIndex + 1));
    data.insert(QString::fromLatin1(DATA_INKS), inks);
    data.insert(QString::fromLatin1(DATA_SPOT_LIMIT_REACHED), result.isSpotLimitReached);
    data.insert(QString::fromLatin1(DATA_PAGE_AREA), result.pageArea);
    data.insert(QStringLiteral("box"), result.isTrimBox ? QStringLiteral("trim") : QStringLiteral("crop"));
    data.insert(QStringLiteral("mediaArea"), result.mediaArea);
    data.insert(QString::fromLatin1(DATA_MAXIMUM_COVERAGE), result.maximumCoverage);
    data.insert(QString::fromLatin1(DATA_AREA_OVER_COVERAGE_LIMIT), result.areaOverCoverageLimit);
    data.insert(QString::fromLatin1(DATA_RICH_BLACK_AREA), result.richBlackArea);
    data.insert(QStringLiteral("coverageLimit"), settings.coverageLimit);
    data.insert(QStringLiteral("richBlackLimit"), settings.richBlackLimit);
    data.insert(QString::fromLatin1(DATA_RENDERING_ISSUES), issues);
    return data;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolOutputPreview::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (errors.empty() && document)
    {
        const qint64 pageCount = qint64(document->getCatalog()->getPageCount());
        const qint64 page = parameters.value(QString::fromLatin1(PARAMETER_PAGE), 1).toLongLong();
        if (pageCount == 0)
        {
            errors.push_back({ QString(), tr("The document has no pages.") });
        }
        else if (page < 1 || page > pageCount)
        {
            errors.push_back({ QString::fromLatin1(PARAMETER_PAGE), tr("Page %1 does not exist – the document has %Ln page(s).", nullptr, int(qMin<qint64>(pageCount, (std::numeric_limits<int>::max)()))).arg(page) });
        }
    }
    return errors;
}

QString PDFDocumentToolOutputPreview::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolOutputPreview::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Analyzer = PDFOutputPreviewAnalyzer;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_PAGE), QT_TR_NOOP("Page"), QT_TR_NOOP("The page (from 1)"), 1, 1, PDFDocumentToolPageSelection::MAXIMUM_PAGE_NUMBER));
    definitions.push_back(Definition::createInteger(tr, QString::fromLatin1(PARAMETER_RESOLUTION), QT_TR_NOOP("Resolution"), QT_TR_NOOP("Pixels of the longer side of the page"),
                                                    1920, 72, PDFOutputPreviewSettings::MAXIMUM_RESOLUTION));

    std::vector<PDFDocumentToolParameterOption> modes;
    modes.push_back({ Analyzer::getModeId(Analyzer::Mode::Separations), QT_TR_NOOP("Separations") });
    modes.push_back({ Analyzer::getModeId(Analyzer::Mode::CoverageWarning), QT_TR_NOOP("Coverage Warning") });
    modes.push_back({ Analyzer::getModeId(Analyzer::Mode::RichBlackWarning), QT_TR_NOOP("Rich Black Warning") });
    modes.push_back({ Analyzer::getModeId(Analyzer::Mode::InkCoverage), QT_TR_NOOP("Ink Coverage") });
    modes.push_back({ Analyzer::getModeId(Analyzer::Mode::ShapeChannel), QT_TR_NOOP("Shape Channel") });
    modes.push_back({ Analyzer::getModeId(Analyzer::Mode::OpacityChannel), QT_TR_NOOP("Opacity Channel") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_MODE), QT_TR_NOOP("Mode"), QT_TR_NOOP("What the preview shows"), std::move(modes),
                                                 Analyzer::getModeId(Analyzer::Mode::Separations)));

    definitions.push_back(Definition::createReal(tr, QString::fromLatin1(PARAMETER_COVERAGE_LIMIT), QT_TR_NOOP("Coverage limit"), QT_TR_NOOP("Total area coverage limit in percent"), 300.0, 0.0, 10000.0));
    definitions.push_back(Definition::createReal(tr, QString::fromLatin1(PARAMETER_RICH_BLACK_LIMIT), QT_TR_NOOP("Rich black limit"), QT_TR_NOOP("Black ink in percent, from which black with another process ink is rich black"), 95.0, 0.0, 100.0));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SIMULATE_SEPARATIONS), QT_TR_NOOP("Simulate separations"), QT_TR_NOOP("The inks are mixed as on the press"), true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SIMULATE_PAPER_COLOR), QT_TR_NOOP("Paper color"), QT_TR_NOOP("The color of the paper is simulated"), false));
    definitions.push_back(Definition::createColor(tr, QString::fromLatin1(PARAMETER_PAPER_COLOR), QT_TR_NOOP("Color of the paper"), nullptr, QColor(Qt::white)));
    definitions.push_back(Definition::createString(tr, QString::fromLatin1(PARAMETER_INACTIVE_INKS), QT_TR_NOOP("Hidden inks"), QT_TR_NOOP("Names of the inks, which are not drawn, separated by commas"), QString(), 4096));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_IMAGES), QT_TR_NOOP("Images"), nullptr, true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_TEXT), QT_TR_NOOP("Text"), nullptr, true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_VECTORS), QT_TR_NOOP("Vector graphics"), nullptr, true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_SHADINGS), QT_TR_NOOP("Shadings"), nullptr, true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_PATTERNS), QT_TR_NOOP("Patterns"), nullptr, true));
    return definitions;
}

PDFDocumentToolResult PDFDocumentToolOutputPreview::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    const PDFDocument* document = context.getDocument();
    if (!document)
    {
        return PDFDocumentToolResult::createFailed(tr("There is no document."));
    }

    // The values of the parameters with the color management and the rendering of the run
    PDFOutputPreviewSettings settings = PDFOutputPreviewSettings::create(document, context.rendering);
    const PDFOutputPreviewSettings parameterSettings = createSettings(parameters);
    settings.resolution = parameterSettings.resolution;
    settings.isSeparationSimulated = parameterSettings.isSeparationSimulated;
    settings.isPaperColorSimulated = parameterSettings.isPaperColorSimulated;
    settings.paperColor = parameterSettings.paperColor;
    settings.content = parameterSettings.content;
    settings.inactiveInks = parameterSettings.inactiveInks;
    settings.coverageLimit = parameterSettings.coverageLimit;
    settings.richBlackLimit = parameterSettings.richBlackLimit;

    if (progress)
    {
        progress->start(1, ProgressStartupInfo());
    }

    const PDFInteger pageIndex = PDFInteger(parameters.value(QString::fromLatin1(PARAMETER_PAGE), 1).toLongLong()) - 1;
    const PDFOutputPreviewResult result = PDFOutputPreviewAnalyzer::render(document, pageIndex, settings, control);

    if (progress)
    {
        progress->step();
        progress->finish();
    }

    if (result.isCanceled || PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    if (!result.errorMessage.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(result.errorMessage);
    }

    PDFDocumentToolResult toolResult;
    toolResult.data = createData(result, settings);
    for (const PDFRenderError& issue : result.renderingIssues)
    {
        toolResult.renderingIssues.push_back({ pageIndex, issue.message });
    }

    // The facts are machine-readable (MCP): the numbers in the C locale - %n, not %Ln
    const QLocale locale = QLocale::c();
    int measuredInks = 0;
    for (const PDFOutputPreviewInk& ink : result.inks)
    {
        measuredInks += ink.channel >= 0 ? 1 : 0;
    }
    toolResult.facts.push_back({ QStringLiteral("inks"), tr("Inks"), tr("%n ink(s)", nullptr, measuredInks) });
    toolResult.facts.push_back({ QStringLiteral("maximumCoverage"), tr("Highest coverage"), tr("%1 %").arg(locale.toString(result.maximumCoverage, 'f', 0)) });
    toolResult.facts.push_back({ QStringLiteral("areaOverCoverageLimit"), tr("Over the coverage limit"), tr("%1 mm²").arg(locale.toString(result.areaOverCoverageLimit, 'f', 2)) });
    toolResult.facts.push_back({ QStringLiteral("richBlackArea"), tr("Rich black"), tr("%1 mm²").arg(locale.toString(result.richBlackArea, 'f', 2)) });
    return toolResult;
}

}   // namespace pdf
