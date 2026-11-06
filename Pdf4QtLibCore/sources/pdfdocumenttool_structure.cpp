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

#include "pdfdocumenttool_structure.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfoperationcontrol.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocale>

#include <algorithm>
#include <limits>

namespace pdf
{

/// Helpers of the tool Optimize Structure
class PDFDocumentToolStructureHelper
{
public:
    PDFDocumentToolStructureHelper() = delete;

    // The texts of the panel Steps (303:4560): the parameters, so also the schema of the assistant
    static constexpr const char* TITLE_EMBED = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Embed (dereference) simple objects, such as int, bool, real");
    static constexpr const char* DESCRIPTION_EMBED = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Small values are written in place instead of being referenced");
    static constexpr const char* TITLE_NULL = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Remove null objects from dictionary entries");
    static constexpr const char* DESCRIPTION_NULL = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Entries without a value are dropped");
    static constexpr const char* TITLE_UNUSED = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Remove unused objects (unreachable from document root)");
    static constexpr const char* DESCRIPTION_UNUSED = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Leftovers of earlier edits are deleted");
    static constexpr const char* TITLE_MERGE = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Merge identical objects");
    static constexpr const char* DESCRIPTION_MERGE = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Duplicates are stored once");
    static constexpr const char* TITLE_SHRINK = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Shrink object storage (squeeze free entries)");
    static constexpr const char* DESCRIPTION_SHRINK = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Objects are renumbered so no gaps remain");
    static constexpr const char* TITLE_RECOMPRESS = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Recompress flate streams by maximal compression");
    static constexpr const char* DESCRIPTION_RECOMPRESS = QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Takes the longest, saves the most");

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// A count for %Ln (a hostile number never overflows the int of tr)
    static int toCount(qint64 count)
    {
        return int(std::clamp(count, qint64(0), qint64(std::numeric_limits<int>::max())));
    }

    /// Counter of the optimizer of a step
    static qint64 getCount(const PDFOptimizer::Statistics& statistics, PDFOptimizer::OptimizationFlag flag)
    {
        switch (flag)
        {
            case PDFOptimizer::DereferenceSimpleObjects:
                return statistics.simpleObjectsDereferenced;
            case PDFOptimizer::RemoveNullObjects:
                return statistics.nullEntriesRemoved;
            case PDFOptimizer::RemoveUnusedObjects:
                return statistics.unusedObjectsRemoved;
            case PDFOptimizer::MergeIdenticalObjects:
                return statistics.identicalObjectsMerged;
            case PDFOptimizer::ShrinkObjectStorage:
                return statistics.objectEntriesShrunk;
            case PDFOptimizer::RecompressFlateStreams:
                return statistics.streamsRecompressed;
            default:
                return 0;
        }
    }

    /// Text of a step, which has run (303:5370: "1,204 objects embedded"); the singular has its
    /// own text, right in English also without a translation
    static QString getCountText(PDFOptimizer::OptimizationFlag flag, qint64 count)
    {
        const int n = toCount(count);
        switch (flag)
        {
            case PDFOptimizer::DereferenceSimpleObjects:
                return n == 1 ? PDFDocumentToolStructure::tr("%Ln object embedded", nullptr, n) : PDFDocumentToolStructure::tr("%Ln objects embedded", nullptr, n);
            case PDFOptimizer::RemoveNullObjects:
                return n == 1 ? PDFDocumentToolStructure::tr("%Ln entry removed", nullptr, n) : PDFDocumentToolStructure::tr("%Ln entries removed", nullptr, n);
            case PDFOptimizer::RemoveUnusedObjects:
                return n == 1 ? PDFDocumentToolStructure::tr("%Ln object removed", nullptr, n) : PDFDocumentToolStructure::tr("%Ln objects removed", nullptr, n);
            case PDFOptimizer::MergeIdenticalObjects:
                return n == 1 ? PDFDocumentToolStructure::tr("%Ln duplicate merged", nullptr, n) : PDFDocumentToolStructure::tr("%Ln duplicates merged", nullptr, n);
            case PDFOptimizer::ShrinkObjectStorage:
                return n == 1 ? PDFDocumentToolStructure::tr("%Ln free entry removed", nullptr, n) : PDFDocumentToolStructure::tr("%Ln free entries removed", nullptr, n);
            case PDFOptimizer::RecompressFlateStreams:
                return n == 1 ? PDFDocumentToolStructure::tr("%Ln stream recompressed", nullptr, n) : PDFDocumentToolStructure::tr("%Ln streams recompressed", nullptr, n);
            default:
                return QString();
        }
    }

    static QString formatInteger(qint64 value)
    {
        return QLocale().toString(value);
    }
};

const std::vector<PDFDocumentToolStructure::Step>& PDFDocumentToolStructure::getSteps()
{
    // steps 319:7288: the short texts of the right panel
    static const std::vector<Step> steps = {
        { PARAMETER_EMBED_SIMPLE_OBJECTS, QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Embed simple objects"), QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Writes small values in place"), "embed", PDFOptimizer::DereferenceSimpleObjects },
        { PARAMETER_REMOVE_NULL_OBJECTS, QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Remove null objects"), QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Drops entries without a value"), "eraser", PDFOptimizer::RemoveNullObjects },
        { PARAMETER_REMOVE_UNUSED_OBJECTS, QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Remove unused objects"), QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Deletes leftovers of earlier edits"), "trash", PDFOptimizer::RemoveUnusedObjects },
        { PARAMETER_MERGE_IDENTICAL_OBJECTS, QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Merge identical objects"), QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Stores duplicates once"), "copy", PDFOptimizer::MergeIdenticalObjects },
        { PARAMETER_SHRINK_OBJECT_STORAGE, QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Shrink object storage"), QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Renumbers objects without gaps"), "hash", PDFOptimizer::ShrinkObjectStorage },
        { PARAMETER_RECOMPRESS_FLATE_STREAMS, QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Recompress flate streams"), QT_TRANSLATE_NOOP("pdf::PDFDocumentToolStructure", "Slowest step, saves the most"), "minimize-2", PDFOptimizer::RecompressFlateStreams },
    };
    return steps;
}

QString PDFDocumentToolStructure::getStepTitle(const Step& step)
{
    return tr(step.title);
}

QString PDFDocumentToolStructure::getStepDescription(const Step& step)
{
    return tr(step.description);
}

PDFOptimizer::OptimizationFlags PDFDocumentToolStructure::getFlags(const QVariantMap& parameters)
{
    PDFOptimizer::OptimizationFlags flags = PDFOptimizer::None;
    for (const Step& step : getSteps())
    {
        flags.setFlag(step.flag, parameters.value(PDFDocumentToolStructureHelper::name(step.parameter)).toBool());
    }
    return flags;
}

PDFDocumentToolStructure::Counts PDFDocumentToolStructure::getCounts(const PDFDocument* document)
{
    Counts counts;
    if (!document)
    {
        return counts;
    }

    for (const PDFObjectStorage::Entry& entry : document->getStorage().getObjects())
    {
        if (!entry.object.isNull())
        {
            ++counts.objects;
            counts.streams += entry.object.isStream() ? 1 : 0;
        }
    }
    return counts;
}

std::vector<PDFDocumentToolStructure::StepRow> PDFDocumentToolStructure::getStepRows(const PDFDocumentToolResult& result)
{
    std::vector<StepRow> rows;
    const QJsonObject steps = result.data.value(QStringLiteral("steps")).toObject();
    if (steps.isEmpty())
    {
        return rows;
    }

    for (const Step& step : getSteps())
    {
        const QJsonObject value = steps.value(PDFDocumentToolStructureHelper::name(step.parameter)).toObject();

        StepRow row;
        row.parameter = PDFDocumentToolStructureHelper::name(step.parameter);
        row.title = getStepTitle(step);
        row.iconName = QString::fromLatin1(step.iconName);
        row.isRun = value.value(QStringLiteral("run")).toBool();
        row.text = row.isRun ? PDFDocumentToolStructureHelper::getCountText(step.flag, value.value(QStringLiteral("count")).toInteger()) : tr("Skipped – not selected");
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<PDFDocumentToolFact> PDFDocumentToolStructure::getResultRows(const PDFDocumentToolResult& result)
{
    using Helper = PDFDocumentToolStructureHelper;

    const qint64 sizeBefore = result.data.value(QStringLiteral("sizeBefore")).toInteger();
    const qint64 sizeAfter = result.data.value(QStringLiteral("sizeAfter")).toInteger();
    const qint64 savedBytes = result.data.value(QStringLiteral("savedBytes")).toInteger();

    std::vector<PDFDocumentToolFact> rows;
    rows.push_back({ QStringLiteral("sizeBefore"), tr("Size before"), PDFDocumentToolFormat::formatSize(sizeBefore) });
    rows.push_back({ QStringLiteral("sizeAfter"), tr("Size after"), PDFDocumentToolFormat::formatSize(sizeAfter) });
    rows.push_back({ QStringLiteral("saved"), tr("Saved"), tr("%1 · %2").arg(PDFDocumentToolFormat::formatSize(savedBytes), formatSavedPercent(sizeBefore, sizeAfter)) });
    rows.push_back({ QStringLiteral("objects"), tr("Objects"), tr("%1 → %2").arg(Helper::formatInteger(result.data.value(QStringLiteral("objectsBefore")).toInteger()),
                                                                               Helper::formatInteger(result.data.value(QStringLiteral("objectsAfter")).toInteger())) });
    rows.push_back({ QStringLiteral("time"), tr("Time"), formatDuration(result.data.value(QStringLiteral("durationMilliseconds")).toInteger()) });
    return rows;
}

QString PDFDocumentToolStructure::createReport(const PDFDocumentToolResult& result)
{
    const PDFDocumentToolCatalogEntry* tool = PDFDocumentToolCatalog::findTool(QString::fromLatin1(TOOL_ID));

    QStringList lines;
    lines << (tool ? tool->getTitle() : QString::fromLatin1(TOOL_ID));
    for (const PDFDocumentToolFact& row : getResultRows(result))
    {
        lines << tr("%1: %2").arg(row.title, row.value);
    }
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        if (fact.id == QLatin1String("conformance"))
        {
            lines << tr("%1: %2").arg(fact.title, fact.value);
        }
    }

    lines << QString() << tr("Steps performed");
    for (const StepRow& row : getStepRows(result))
    {
        lines << tr("%1: %2").arg(row.title, row.text);
    }
    return lines.join(QLatin1Char('\n'));
}

QString PDFDocumentToolStructure::formatDuration(qint64 milliseconds)
{
    if (milliseconds >= 60000)
    {
        return PDFDocumentToolFormat::formatDuration(milliseconds);
    }

    // At least the tenth of a second, a run is never "0.0 s"
    const double seconds = double(qMax(qint64(100), milliseconds)) / 1000.0;
    return tr("%1 s").arg(QLocale().toString(seconds, 'f', 1));
}

QString PDFDocumentToolStructure::formatSavedPercent(qint64 before, qint64 after)
{
    const double percent = before > 0 && after < before ? double(before - after) * 100.0 / double(before) : 0.0;
    return tr("%1 %").arg(QLocale().toString(percent, 'f', 1));
}

QString PDFDocumentToolStructure::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolStructure::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolStructureHelper;

    // Every step is on by default (302:4072 "Steps: 6 of 6")
    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_EMBED_SIMPLE_OBJECTS), Helper::TITLE_EMBED, Helper::DESCRIPTION_EMBED, true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_REMOVE_NULL_OBJECTS), Helper::TITLE_NULL, Helper::DESCRIPTION_NULL, true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_REMOVE_UNUSED_OBJECTS), Helper::TITLE_UNUSED, Helper::DESCRIPTION_UNUSED, true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_MERGE_IDENTICAL_OBJECTS), Helper::TITLE_MERGE, Helper::DESCRIPTION_MERGE, true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_SHRINK_OBJECT_STORAGE), Helper::TITLE_SHRINK, Helper::DESCRIPTION_SHRINK, true));
    definitions.push_back(Definition::createBool(tr, Helper::name(PARAMETER_RECOMPRESS_FLATE_STREAMS), Helper::TITLE_RECOMPRESS, Helper::DESCRIPTION_RECOMPRESS, true));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolStructure::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (getFlags(parameters) == PDFOptimizer::None)
    {
        errors.push_back({ QString::fromLatin1(PARAMETER_EMBED_SIMPLE_OBJECTS), tr("Choose at least one step: set at least one of the six parameters to true.") });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolStructure::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolStructureHelper;

    QElapsedTimer timer;
    timer.start();

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const PDFOptimizer::OptimizationFlags flags = getFlags(parameters);

    const qint64 sizeBefore = getWholeFileInputSize(input);
    if (sizeBefore < 0)
    {
        return PDFDocumentToolResult::createFailed(tr("The size of the document cannot be measured."));
    }

    // One bar over the phases: the stages of the optimizer and the written file
    PDFProgressPhases phases(progress);
    const int structurePhase = phases.addPhase(85, tr("Cleaning up the structure"), true);
    const int measurePhase = phases.addPhase(15, tr("Measuring the result"), false);
    phases.start();

    // A local optimizer without a parent: it has a state, one instance of the tool serves more threads
    PDFOptimizer optimizer(flags, nullptr);
    optimizer.setOperationControl(control);
    optimizer.setProgress(phases.getPhase(structurePhase));
    optimizer.setStorage(document->getStorage());
    optimizer.optimize();
    const PDFOptimizer::Statistics statistics = optimizer.getStatistics();

    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // The whole file without its dead containers, with the version of the input; a file, which
    // is rewritten, is not checked against a standard it declares
    ConformanceRemoval conformance;
    PDFDocument output = createWholeFileDocument(optimizer.takeStorage(), document, &conformance);
    if (!conformance.kept.isEmpty())
    {
        return createConformanceFailure(conformance.kept);
    }

    phases.beginPhase(measurePhase);
    const WholeFileSize outputSize = getWholeFileOutputSize(&output);
    if (!outputSize.error.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(outputSize.error);
    }
    const qint64 sizeAfter = outputSize.size;
    if (PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    phases.finish();

    const qint64 duration = timer.elapsed();
    const Counts countsBefore = getCounts(document);
    const Counts countsAfter = getCounts(&output);

    PDFDocumentToolResult result;
    if (sizeAfter >= sizeBefore)
    {
        result = PDFDocumentToolResult::createNothingToDo(tr("File cannot be reduced with these steps"));
    }

    // The steps: data of the panel and of the report, rows of MCP
    QJsonObject stepsData;
    int stepsRun = 0;
    for (const Step& step : getSteps())
    {
        const bool isRun = flags.testFlag(step.flag);
        const qint64 count = Helper::getCount(statistics, step.flag);

        QJsonObject stepData;
        stepData.insert(QStringLiteral("run"), isRun);
        stepData.insert(QStringLiteral("count"), isRun ? count : 0);
        stepsData.insert(Helper::name(step.parameter), stepData);
        stepsRun += isRun ? 1 : 0;

        PDFDocumentToolStep resultStep;
        resultStep.iconName = QString::fromLatin1(step.iconName);
        resultStep.text = tr("%1: %2").arg(getStepTitle(step), isRun ? Helper::getCountText(step.flag, count) : tr("Skipped – not selected"));
        resultStep.state = isRun ? PDFDocumentToolStep::State::Done : PDFDocumentToolStep::State::Skipped;
        result.steps.push_back(std::move(resultStep));
    }

    // The facts of the result dialog 303:4897
    const int stepCount = int(getSteps().size());
    const QString sizeValue = sizeAfter < sizeBefore ? tr("%1 (%2 smaller)").arg(PDFDocumentToolFormat::formatSizeChange(sizeBefore, sizeAfter), formatSavedPercent(sizeBefore, sizeAfter))
                                                     : PDFDocumentToolFormat::formatSizeChange(sizeBefore, sizeAfter);
    const QString stepsValue = stepsRun == stepCount ? tr("%1 of %2 run").arg(stepsRun).arg(stepCount)
                                                     : tr("%1 of %2 run · %3 skipped").arg(stepsRun).arg(stepCount).arg(stepCount - stepsRun);
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), sizeValue });
    result.facts.push_back({ QStringLiteral("steps"), tr("Steps"), stepsValue });
    result.facts.push_back({ QStringLiteral("time"), tr("Time"), formatDuration(duration) });

    // Only a result, which exists, has lost its declaration
    if (sizeAfter < sizeBefore)
    {
        addConformanceFact(&result, conformance.removed);
    }

    result.data.insert(QStringLiteral("objectsBefore"), countsBefore.objects);
    result.data.insert(QStringLiteral("objectsAfter"), countsAfter.objects);
    result.data.insert(QStringLiteral("stepCount"), stepCount);
    result.data.insert(QStringLiteral("stepsRun"), stepsRun);
    result.data.insert(QStringLiteral("steps"), stepsData);
    result.data.insert(QStringLiteral("recompressedBytesSaved"), statistics.recompressedBytesSaved);
    result.data.insert(QStringLiteral("durationMilliseconds"), duration);

    finishWholeFileResult(&result, input, std::move(output), sizeBefore, sizeAfter, QStringLiteral("_optimized"));
    return result;
}

}   // namespace pdf
